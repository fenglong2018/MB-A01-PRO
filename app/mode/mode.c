/**
 * @file mode.c
 * @brief 整机模式状态机任务（规则见 fsm.md：告警节奏 / FAKE_OFF / 逻辑告警按键）
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include "config.h"
#include "mode.h"
#include "session_alarm.h"
#include "board_pins.h"
#include "board_gpio.h"
#include "key.h"
#include "pwr_plna.h"
#include "bkp_user.h"
#if USE_RTC
#include "rtc.h"
#endif
#if USE_PM
#include "pm.h"
#endif
#include "iwdg.h"
#if USE_ADC_BAT
#include "adc_bat.h"
#endif
#if USE_LED
#include "led.h"
#endif
#if USE_GNSS
#include "gnss.h"
#endif
#if USE_RDSS
#include "rdss.h"
#endif
#include "ulog_cdc_be.h"
#if USE_USB_CDC
#include "cdc_acm.h"
#endif

static struct rt_event s_mode_evt;
static struct rt_thread s_mode_thread;
static rt_uint8_t s_mode_stack[MODE_THREAD_STACK];

static mode_state_t s_state = MODE_ST_OFF;
static mode_state_t s_prev = MODE_ST_OFF;       /* CHARGE/BATT/ALARM 等底层返回 */
static mode_state_t s_pt_resume = MODE_ST_OFF;  /* 进入 PASSTHRU 前的 ON/CHARGE */
static mode_state_t s_fake_resume = MODE_ST_OFF; /* FAKE_OFF 的逻辑 ON/ALARM */
static uint8_t s_batt_from_alarm;               /* BATT 由告警短按叠上看电 */
static uint32_t s_alarm_unix;                   /* 告警节奏锚点（未校时为 0） */
static mode_state_t s_lb_resume = MODE_ST_OFF;  /* LOW_BATT 结束后回去 */
static uint8_t s_lb_sent;                       /* 本轮 WARN 已发 1 条 N（BKP，防 STOP2 重复） */
static uint8_t s_pm_run;   /* 非 FAKE_OFF 时 lock，禁止 idle STOP0 */
static uint8_t s_pt_flags;
static uint8_t s_pt_req_flags;
static rt_tick_t s_batt_deadline;
static rt_uint8_t s_batt_armed;
static rt_uint8_t s_nosim_hint;     /* 无卡 ON 正在 5s 灯提示，尚未真关机 */
static rt_tick_t s_nosim_hint_deadline;
static rt_uint8_t s_ready;
static rt_uint8_t s_evt_ok;
static rt_uint32_t s_evt_pend;
#if USE_LED
static mode_state_t s_ui_view = (mode_state_t)0xFF;
#endif

static void passthru_radio_apply(uint8_t want);
static void passthru_radio_stop_all(void);
static void passthru_ulog_hold(int hold);
static int usb_is_present(void);
static void handle_bat_warn(void);
static void batt_disarm(void);
static void enter_fake_off(void);
static void enter_off_from_on(void);
static void hw_off_rails_led(void);

static mode_state_t mode_led_view(void)
{
    /*
     * 耦合说明：FSM 保持 PASSTHRU；仅 LED 视图在插 USB 时显示 CHARGE。
     * 状态机与充电策略不因 LED 改变。
     */
    if (s_state == MODE_ST_PASSTHRU)
    {
        return usb_is_present() ? MODE_ST_CHARGE : MODE_ST_OFF;
    }
    if (s_state == MODE_ST_FAKE_OFF)
    {
        /* ALARM 假关机双闪；有卡 ON 假关机仍 10s 单闪；无卡灭灯 */
        if (s_fake_resume == MODE_ST_ALARM)
        {
            return MODE_ST_ALARM;
        }
        return sim_present() ? MODE_ST_ON : MODE_ST_OFF;
    }
    return s_state;
}

static void mode_ui_sync(void)
{
#if USE_LED
    mode_state_t view = mode_led_view();

    if (s_ui_view == view)
    {
        return;
    }
    s_ui_view = view;
    led_post_mode((uint8_t)view);
#endif
}

static const char *mode_name(mode_state_t st)
{
    switch (st)
    {
    case MODE_ST_OFF:       return "OFF";
    case MODE_ST_BATT:      return "BATT";
    case MODE_ST_ON:        return "ON";
    case MODE_ST_ALARM:     return "ALARM";
    case MODE_ST_CHARGE:    return "CHARGE";
    case MODE_ST_FORCE_OFF: return "FORCE_OFF";
    case MODE_ST_PASSTHRU:  return "PASSTHRU";
    case MODE_ST_LOW_BATT:  return "LOW_BATT";
    case MODE_ST_FAKE_OFF:  return "FAKE_OFF";
    default:                return "?";
    }
}

mode_state_t mode_state_get(void)
{
    return s_state;
}

uint8_t mode_passthru_flags_get(void)
{
    return (s_state == MODE_ST_PASSTHRU) ? s_pt_flags : 0;
}

void mode_post_event(rt_uint32_t evt)
{
    if (evt == 0)
    {
        return;
    }
    /* STOP2 醒来 KEY 可能比 MODE 线程更早跑完一次短按，不能丢 */
    if (!s_evt_ok)
    {
        s_evt_pend |= evt;
        return;
    }
    rt_event_send(&s_mode_evt, evt);
}

#if USE_RTC
static void mode_on_rtc_wu(void)
{
    mode_post_event(MODE_EVT_RTC_WU);
}
#endif

int mode_passthru_set(uint8_t flags)
{
    if (!s_ready)
    {
        return -RT_ERROR;
    }
    s_pt_req_flags = flags;
    rt_event_send(&s_mode_evt, MODE_EVT_PT_APPLY);
    return RT_EOK;
}

static int usb_is_present(void)
{
    return board_usb_inserted();
}

static int bat_is_protect(void)
{
#if USE_ADC_BAT
    return (adc_bat_get_level() == ADC_BAT_LVL_PROTECT);
#else
    return 0;
#endif
}

static int iwdg_should_run(void)
{
    if ((s_state == MODE_ST_OFF) || (s_state == MODE_ST_FORCE_OFF))
    {
        return 0;
    }
    /* 关机看电 5s 不要把狗打开，否则回 OFF 后关不掉 */
    if ((s_state == MODE_ST_BATT) && !s_batt_from_alarm &&
        ((s_prev == MODE_ST_OFF) || (s_prev == MODE_ST_FORCE_OFF)))
    {
        return 0;
    }
    return 1;
}

static void iwdg_for_alive(void)
{
    if (!iwdg_should_run())
    {
        return;
    }
    iwdg_start();
}

static void pm_run_sync(void)
{
#if USE_PM
    /*
     * 默认不进 STOP0。只有 FAKE_OFF（逻辑仍是 ON/ALARM，等 RTC 10s）放行。
     * 物理 ON/ALARM 发信、看电、充电、透传都要 SysTick，不能睡。
     */
    int want = (s_state != MODE_ST_FAKE_OFF);

    if (want && !s_pm_run)
    {
        pm_lock();
        s_pm_run = 1;
    }
    else if (!want && s_pm_run)
    {
        pm_unlock();
        s_pm_run = 0;
    }
#endif
}

static void hb_start(void)
{
#if USE_LED
    /* 假关机脉冲靠 RTC：ALARM 双闪；有卡 ON 单闪；无卡灭灯 */
    if ((s_fake_resume == MODE_ST_ALARM) ||
        ((s_fake_resume == MODE_ST_ON) && sim_present()))
    {
        led_wait_rtc_hb(1);
    }
    else
    {
        led_wait_rtc_hb(0);
    }
#endif
#if USE_RTC
    (void)rtc_wu_start(10);
#endif
}

static void hb_stop(void)
{
#if USE_RTC
    rtc_wu_stop();
#endif
#if USE_LED
    led_wait_rtc_hb(0);
#endif
}

/** 有卡 ON：10s 闪用 RTC WakeUp，搜星和假关机同一条节拍 */
static void on_led_rtc_begin(void)
{
    if (!sim_present())
    {
#if USE_LED
        led_use_rtc_hb(0);
#endif
        return;
    }
#if USE_RTC
    (void)rtc_wu_start(10);
#endif
#if USE_LED
    led_use_rtc_hb(1);
#endif
}

/**
 * 真关机前该不该再等一会儿：>0 = 还要等这么多 tick，0 = 可以睡。
 *
 * mode 线程优先级比 key 高，OFF 下 maybe_stop2() 不阻塞，
 * 不挡一下的话 key 线程一次都跑不到，STOP2 醒来那次按下永远进不了状态机。
 */
static rt_int32_t stop2_defer_ticks(void)
{
    rt_int32_t left;

    if ((s_state != MODE_ST_OFF) && (s_state != MODE_ST_FORCE_OFF))
    {
        return 0;
    }
    /* 上电头一段：等各 INIT_APP 跑完，KEY 才有机会接管复位时的电平 */
    left = (rt_int32_t)(rt_tick_from_millisecond(MODE_BOOT_HOLD_MS) - rt_tick_get());
    if (left > 0)
    {
        return left;
    }
    if (key_is_busy())
    {
        return (rt_int32_t)rt_tick_from_millisecond(KEY_POLL_MS);
    }
    return 0;
}

static void maybe_stop2(void)
{
    if (usb_is_present())
    {
        return;
    }
    if (s_batt_armed && (s_state == MODE_ST_BATT))
    {
        return;
    }
    if ((s_state != MODE_ST_OFF) && (s_state != MODE_ST_FORCE_OFF))
    {
        return;
    }
    if (stop2_defer_ticks() > 0)
    {
        return;
    }
    hb_stop();
    iwdg_stop2_quiet();
#if USE_USB_CDC
    usb_cdc_stop();
#endif
    /* 灯是消息驱动：LED 线程可能还没处理 OFF，GPIO 仍低有效点亮就进 STOP2 */
#if USE_LED
    led_output_off();
#endif
    hw_off_rails_led();
#if USE_PM
    pm_stop2_enter();
#endif
}

typedef struct
{
    uint8_t  mode;
    uint8_t  prev;
    uint8_t  alarm_active;
    uint8_t  resume;           /* FAKE_OFF 时为 ON/ALARM */
    uint32_t alarm_start_unix;
    uint8_t  lb_sent;          /* 本轮 WARN 已发过 1 条 N；回到 OK 清 */
    uint8_t  rsv[3];
} mode_sticky_t;

static int logic_is_alarm(void)
{
    if (s_state == MODE_ST_ALARM)
    {
        return 1;
    }
    if ((s_state == MODE_ST_FAKE_OFF) && (s_fake_resume == MODE_ST_ALARM))
    {
        return 1;
    }
    if ((s_state == MODE_ST_BATT) && s_batt_from_alarm)
    {
        return 1;
    }
    return 0;
}

static uint32_t sticky_unix_now(void)
{
#if USE_RTC
    return rtc_get_unix();
#else
    return 0;
#endif
}

static void sticky_commit(mode_state_t st, uint8_t alarm_on, uint32_t alarm_unix)
{
    mode_sticky_t s;

    if ((st != MODE_ST_ALARM) && (st != MODE_ST_OFF) && (st != MODE_ST_FORCE_OFF) &&
        (st != MODE_ST_FAKE_OFF))
    {
        return;
    }
    memset(&s, 0, sizeof(s));
    s.mode = (uint8_t)st;
    s.prev = (uint8_t)s_prev;
    s.alarm_active = alarm_on ? 1u : 0u;
    s.resume = (uint8_t)s_fake_resume;
    s.alarm_start_unix = alarm_on ? alarm_unix : 0u;
    s.lb_sent = s_lb_sent ? 1u : 0u;
    (void)bkp_user_save(&s, (uint16_t)sizeof(s));
}

/** 只刷新 BKP 里的 lb_sent，不改 MODE/告警粘性 */
static void sticky_persist_lb_sent(void)
{
    mode_sticky_t s;

    if (bkp_user_load(&s, (uint16_t)sizeof(s)) != (int)sizeof(s))
    {
        memset(&s, 0, sizeof(s));
        s.mode = (uint8_t)MODE_ST_OFF;
        s.prev = (uint8_t)s_prev;
    }
    s.lb_sent = s_lb_sent ? 1u : 0u;
    (void)bkp_user_save(&s, (uint16_t)sizeof(s));
}

void mode_alarm_anchor_latch(uint32_t unix_sec)
{
    if ((unix_sec == 0) || !logic_is_alarm())
    {
        return;
    }
    s_alarm_unix = unix_sec;
    sticky_commit((s_state == MODE_ST_FAKE_OFF) ? MODE_ST_FAKE_OFF : MODE_ST_ALARM, 1, unix_sec);
}

static int sticky_try_restore(void)
{
    mode_sticky_t s;
    uint32_t now;
    uint32_t elapsed;

    if (bkp_user_load(&s, (uint16_t)sizeof(s)) != (int)sizeof(s))
    {
        s_lb_sent = 0;
        return 0;
    }

    s_prev = (mode_state_t)s.prev;
    s_lb_sent = s.lb_sent ? 1u : 0u;

    if (s.mode == (uint8_t)MODE_ST_FORCE_OFF)
    {
        return 0; /* 仍走后面 ADC/USB；告警已作废 */
    }

    if (s.alarm_active &&
        ((s.mode == (uint8_t)MODE_ST_ALARM) ||
         ((s.mode == (uint8_t)MODE_ST_FAKE_OFF) && (s.resume == (uint8_t)MODE_ST_ALARM))))
    {
        now = sticky_unix_now();
        if ((s.alarm_start_unix == 0) || (now == 0) || (now < s.alarm_start_unix))
        {
            elapsed = 0;
        }
        else
        {
            elapsed = now - s.alarm_start_unix;
        }
        batt_disarm();
        s_fake_resume = MODE_ST_ALARM;
        s_alarm_unix = s.alarm_start_unix;
        s_state = MODE_ST_ALARM;
        session_alarm_resume(s.alarm_start_unix);
        sticky_commit(MODE_ST_ALARM, 1, s.alarm_start_unix);
        rt_kprintf("[MODE] restore ALARM unix=%lu elapsed=%lu\n",
                   (unsigned long)s.alarm_start_unix, (unsigned long)elapsed);
        return 1;
    }

    if (s.mode == (uint8_t)MODE_ST_OFF)
    {
        s_state = MODE_ST_OFF;
        return 0;
    }
    return 0;
}

#if USE_ADC_BAT
static void adc_on_batt_enter(void)
{
    adc_bat_request();
}
static void adc_on_charge_enter(void)
{
    adc_bat_set_charging(1);
}
static void adc_on_charge_leave(void)
{
    adc_bat_set_charging(0);
}
static void adc_on_passthru_enter(void)
{
    adc_bat_pause();
}
#else
static void adc_on_batt_enter(void) {}
static void adc_on_charge_enter(void) {}
static void adc_on_charge_leave(void) {}
static void adc_on_passthru_enter(void) {}
#endif

static void batt_arm(void)
{
    s_batt_armed = 1;
    s_batt_deadline = rt_tick_get() + rt_tick_from_millisecond(MODE_BATT_MS);
}

static void batt_disarm(void)
{
    s_batt_armed = 0;
}

static void nosim_hint_disarm(void)
{
    if (!s_nosim_hint)
    {
        return;
    }
    s_nosim_hint = 0;
#if USE_LED
    led_set_on_period_ms(0);
#endif
}

#if MODE_ON_NOSIM_HINT_MS > 0
static void nosim_hint_arm(void)
{
    s_nosim_hint = 1;
    s_nosim_hint_deadline = rt_tick_get() +
                            rt_tick_from_millisecond(MODE_ON_NOSIM_HINT_MS);
#if USE_LED
    led_dark_unlock();
    led_set_on_period_ms(LED_ON_NOSIM_PERIOD_MS);
    s_ui_view = (mode_state_t)0xFF;
#endif
}

static void handle_nosim_hint_to(void)
{
    if (!s_nosim_hint)
    {
        return;
    }
    nosim_hint_disarm();
    if (s_state != MODE_ST_ON)
    {
        return;
    }
    if (usb_is_present())
    {
        return;
    }
    if (sim_present())
    {
        on_led_rtc_begin();
        enter_fake_off();
        return;
    }
    rt_kprintf("[MODE] ON no-SIM hint done -> OFF\n");
    enter_off_from_on();
}
#else
static void nosim_hint_arm(void)
{
}

static void handle_nosim_hint_to(void)
{
    nosim_hint_disarm();
    if ((s_state == MODE_ST_ON) && !usb_is_present() && !sim_present())
    {
        enter_off_from_on();
    }
}
#endif

static void passthru_ulog_hold(int hold)
{
    ulog_cdc_passthru_hold(hold);
}

static void hw_off_rails_led(void)
{
#if USE_GNSS
    gnss_on_bat_protect();
#endif
#if USE_RDSS
    rdss_on_bat_protect();
#endif
    board_gpio_outputs_off();
    pwr_plna_force_off();
}

static void passthru_radio_stop_all(void)
{
#if USE_GNSS
    gnss_on_bat_protect();
#endif
#if USE_RDSS
    rdss_on_bat_protect();
#endif
    s_pt_flags = 0;
}

static void passthru_radio_apply(uint8_t want)
{
    uint8_t old = s_pt_flags;

#if USE_GNSS
    if ((want & MODE_PT_GNSS) && !(old & MODE_PT_GNSS))
    {
        if (gnss_passthru_enter() != RT_EOK)
        {
            want &= (uint8_t)~MODE_PT_GNSS;
            rt_kprintf("[MODE] GNSS passthru enter fail\n");
        }
    }
    else if (!(want & MODE_PT_GNSS) && (old & MODE_PT_GNSS))
    {
        (void)gnss_passthru_exit();
    }
#else
    want &= (uint8_t)~MODE_PT_GNSS;
#endif

#if USE_RDSS
    if ((want & MODE_PT_RDSS) && !(old & MODE_PT_RDSS))
    {
        if (rdss_passthru_enter() != RT_EOK)
        {
            want &= (uint8_t)~MODE_PT_RDSS;
            rt_kprintf("[MODE] RDSS passthru enter fail\n");
        }
    }
    else if (!(want & MODE_PT_RDSS) && (old & MODE_PT_RDSS))
    {
        (void)rdss_passthru_exit();
    }
#else
    want &= (uint8_t)~MODE_PT_RDSS;
#endif

    s_pt_flags = want;
}

/** 退出透传，回到 resume（考虑 USB / 保护） */
static void exit_passthru(void)
{
    mode_state_t resume = s_pt_resume;

    passthru_radio_stop_all();
    passthru_ulog_hold(0);
    s_pt_resume = MODE_ST_OFF;

    if (usb_is_present())
    {
        if (resume != MODE_ST_CHARGE)
        {
            s_prev = resume; /* 多为 ON */
        }
        s_state = MODE_ST_CHARGE;
        rt_kprintf("[MODE] PASSTHRU -> CHARGE (usb)\n");
        adc_on_charge_enter();
        return;
    }

    if (resume == MODE_ST_CHARGE)
    {
        s_state = s_prev;
    }
    else
    {
        s_state = resume;
    }
    if (s_state == MODE_ST_ON)
    {
        on_led_rtc_begin();
        session_on_start();
    }
    rt_kprintf("[MODE] PASSTHRU -> %s\n", mode_name(s_state));
}

static void enter_force_off(void)
{
    if (s_state == MODE_ST_FORCE_OFF)
    {
        return;
    }
    /* 充电中忽略保护（保持 CHARGE） */
    if (s_state == MODE_ST_CHARGE)
    {
        return;
    }

    if (s_state == MODE_ST_PASSTHRU)
    {
        passthru_radio_stop_all();
        passthru_ulog_hold(0);
        s_pt_resume = MODE_ST_OFF;
        if (usb_is_present())
        {
            s_state = MODE_ST_CHARGE;
            s_prev = MODE_ST_OFF;
            rt_kprintf("[MODE] PASSTHRU -> CHARGE (protect+usb)\n");
            adc_on_charge_enter();
            return;
        }
    }

    session_stop_all();

    batt_disarm();
    s_batt_from_alarm = 0;
    nosim_hint_disarm();
    s_fake_resume = MODE_ST_OFF;
    s_alarm_unix = 0;
    hb_stop();
    s_state = MODE_ST_FORCE_OFF;
    s_prev = MODE_ST_FORCE_OFF;
    sticky_commit(MODE_ST_FORCE_OFF, 0, 0);
    hw_off_rails_led();
    rt_kprintf("[MODE] -> FORCE_OFF (bat protect)\n");
}

static mode_state_t alarm_from_state(void)
{
    if (s_state == MODE_ST_FAKE_OFF)
    {
        return s_fake_resume;
    }
    if (s_state == MODE_ST_BATT)
    {
        return s_prev;
    }
    if (s_state == MODE_ST_LOW_BATT)
    {
        return s_lb_resume;
    }
    return s_state;
}

static void enter_alarm(mode_state_t from)
{
    if (logic_is_alarm())
    {
        return;
    }
    if ((s_state == MODE_ST_FORCE_OFF) || bat_is_protect())
    {
        rt_kprintf("[MODE] ALARM blocked (force/protect)\n");
        return;
    }
    if (s_state == MODE_ST_PASSTHRU)
    {
        rt_kprintf("[MODE] ALARM blocked (exit passthru first)\n");
        return;
    }
    batt_disarm();
    s_batt_from_alarm = 0;
    nosim_hint_disarm();
    if (from == MODE_ST_LOW_BATT)
    {
        from = s_lb_resume;
    }
    if (from == MODE_ST_FAKE_OFF)
    {
        from = s_fake_resume;
    }
    if (from == MODE_ST_BATT)
    {
        from = s_prev;
    }
    /* 从 CHARGE 进告警：底层 prev 已在 s_prev，勿覆盖成 CHARGE */
    if (from != MODE_ST_CHARGE)
    {
        s_prev = from;
    }
    session_stop_all();
    s_fake_resume = MODE_ST_ALARM;
    s_state = MODE_ST_ALARM;
    s_alarm_unix = sticky_unix_now();
    sticky_commit(MODE_ST_ALARM, 1, s_alarm_unix);
    session_alarm_start();
    iwdg_for_alive();
    rt_kprintf("[MODE] -> ALARM (prev=%s)\n", mode_name(s_prev));
}

static void enter_charge(mode_state_t from_prev)
{
    if (logic_is_alarm())
    {
        return;
    }
    if (s_state == MODE_ST_PASSTHRU)
    {
        /* 透传中插 USB：保持 PASSTHRU，仅刷新 LED */
        return;
    }
    batt_disarm();
    s_batt_from_alarm = 0;
    nosim_hint_disarm();
    s_fake_resume = MODE_ST_OFF;
    if (from_prev == MODE_ST_FORCE_OFF)
    {
        from_prev = MODE_ST_OFF;
    }
    session_stop_all();
    s_prev = from_prev;
    s_state = MODE_ST_CHARGE;
    hb_stop();
#if USE_USB_CDC
    usb_cdc_start();
#endif
    iwdg_for_alive();
    rt_kprintf("[MODE] -> CHARGE (prev=%s)\n", mode_name(s_prev));
    adc_on_charge_enter();
}

static void exit_alarm(void)
{
    mode_state_t back;

    if (!logic_is_alarm())
    {
        return;
    }
    session_stop_all();
    batt_disarm();
    s_batt_from_alarm = 0;
    s_fake_resume = MODE_ST_OFF;
    s_alarm_unix = 0;
    back = s_prev;
    if ((back == MODE_ST_ALARM) || (back == MODE_ST_FAKE_OFF) ||
        (back == MODE_ST_BATT) || (back == MODE_ST_LOW_BATT))
    {
        back = MODE_ST_ON;
    }
    sticky_commit(MODE_ST_OFF, 0, 0);
    if (usb_is_present())
    {
        s_state = MODE_ST_CHARGE;
        s_prev = back;
        rt_kprintf("[MODE] ALARM -> CHARGE (usb, prev=%s)\n", mode_name(s_prev));
        adc_on_charge_enter();
    }
    else
    {
        s_state = back;
        rt_kprintf("[MODE] ALARM -> %s\n", mode_name(s_state));
        if (s_state == MODE_ST_ON)
        {
            on_led_rtc_begin();
            session_on_start();
        }
        if (bat_is_protect())
        {
            enter_force_off();
        }
    }
}

static void peek_batt_from_alarm(void)
{
    s_batt_from_alarm = 1;
    /* 不改 s_prev：退出告警仍回进入前的 ON/OFF */
    s_state = MODE_ST_BATT;
    batt_arm();
    rt_kprintf("[MODE] ALARM -> BATT (peek)\n");
    adc_on_batt_enter();
}

static void enter_batt_peek(mode_state_t from)
{
    s_prev = from;
    s_batt_from_alarm = 0;
    s_state = MODE_ST_BATT;
    batt_arm();
    rt_kprintf("[MODE] %s -> BATT (wake)\n", mode_name(from));
    adc_on_batt_enter();
}

static void enter_fake_off(void)
{
    if (usb_is_present())
    {
        return;
    }
    if ((s_state != MODE_ST_ON) && (s_state != MODE_ST_ALARM))
    {
        return;
    }
    nosim_hint_disarm();
    s_fake_resume = s_state;
    s_state = MODE_ST_FAKE_OFF;
    if (s_fake_resume == MODE_ST_ALARM)
    {
        sticky_commit(MODE_ST_FAKE_OFF, 1, s_alarm_unix);
    }
    hb_start();
#if USE_LED
    if ((s_fake_resume == MODE_ST_ALARM) ||
        ((s_fake_resume == MODE_ST_ON) && sim_present()))
    {
        /* 视图仍是 ALARM/ON，mode_ui_sync 会跳过；须让 LED 收到 wait_hb 后的切态 */
        s_ui_view = (mode_state_t)0xFF;
    }
    else
    {
        /* 无卡 ON 假关机灯必须先灭再 STOP0，否则 GPIO 亮着被冻住 */
        led_output_off();
        s_ui_view = MODE_ST_OFF;
    }
#endif
    rt_kprintf("[MODE] -> FAKE_OFF (resume=%s sim=%d)\n",
               mode_name(s_fake_resume), sim_present());
}

static void handle_shot_busy(void)
{
    if (s_state != MODE_ST_FAKE_OFF)
    {
        return;
    }
    if (s_fake_resume == MODE_ST_ALARM)
    {
        /* 告警发信期间双闪仍靠 tick；拍完假关机再开 RTC */
        hb_stop();
#if USE_LED
        led_dark_unlock();
        s_ui_view = (mode_state_t)0xFF;
#endif
    }
    else
    {
        /* 有卡 ON：RTC 10s 不停，搜星阶段继续闪 */
        on_led_rtc_begin();
#if USE_LED
        led_dark_unlock();
#endif
    }
    s_state = s_fake_resume;
    iwdg_for_alive();
    rt_kprintf("[MODE] FAKE_OFF -> %s (shot)\n", mode_name(s_state));
}

static void enter_off_from_on(void)
{
    session_stop_all();
    nosim_hint_disarm();
    s_fake_resume = MODE_ST_OFF;
    hb_stop();
    s_state = MODE_ST_OFF;
    s_prev = MODE_ST_OFF;
    sticky_commit(MODE_ST_OFF, 0, 0);
#if USE_LED
    led_output_off();
#endif
    hw_off_rails_led();
    if (bat_is_protect())
    {
        enter_force_off();
    }
}

static void handle_shot_idle(void)
{
    if (s_state == MODE_ST_ON)
    {
        if (session_last_was_nosim())
        {
#if MODE_ON_NOSIM_HINT_MS > 0
            if (!s_nosim_hint)
            {
                rt_kprintf("[MODE] ON no-SIM hint %ums then OFF\n",
                           (unsigned)MODE_ON_NOSIM_HINT_MS);
                nosim_hint_arm();
            }
            return;
#else
            rt_kprintf("[MODE] ON no-SIM -> OFF\n");
            enter_off_from_on();
            return;
#endif
        }
        nosim_hint_disarm();
    }
    enter_fake_off();
}

static void handle_pt_apply(void)
{
    uint8_t want = s_pt_req_flags;

    if (want == 0)
    {
        if (s_state == MODE_ST_PASSTHRU)
        {
            exit_passthru();
            if (bat_is_protect() && (s_state != MODE_ST_CHARGE))
            {
                enter_force_off();
            }
        }
        return;
    }

    if (bat_is_protect() && !usb_is_present())
    {
        rt_kprintf("[MODE] passthru denied (protect)\n");
        return;
    }

    if (s_state == MODE_ST_PASSTHRU)
    {
        passthru_radio_apply(want);
        if (s_pt_flags == 0)
        {
            exit_passthru();
        }
        else
        {
            rt_kprintf("[MODE] PASSTHRU flags=0x%02x\n", s_pt_flags);
        }
        return;
    }

    if ((s_state != MODE_ST_ON) && (s_state != MODE_ST_CHARGE))
    {
        rt_kprintf("[MODE] passthru denied (state=%s)\n", mode_name(s_state));
        return;
    }

    session_stop_all();
    nosim_hint_disarm();
    s_pt_resume = s_state;
    hb_stop();
    s_state = MODE_ST_PASSTHRU;
    s_pt_flags = 0;
    passthru_ulog_hold(1); /* 先静音，再开模块 UART，避免 ulog 插进 NMEA */
    passthru_radio_apply(want);
    if (s_pt_flags == 0)
    {
        /* 全部通道失败：回退 */
        passthru_ulog_hold(0);
        s_state = s_pt_resume;
        s_pt_resume = MODE_ST_OFF;
        rt_kprintf("[MODE] passthru enter failed, back %s\n", mode_name(s_state));
        return;
    }
    rt_kprintf("[MODE] %s -> PASSTHRU flags=0x%02x\n",
               mode_name(s_pt_resume), s_pt_flags);
    adc_on_passthru_enter();
}

static void handle_sos_short(void)
{
    switch (s_state)
    {
    case MODE_ST_OFF:
    case MODE_ST_FORCE_OFF:
        enter_batt_peek(s_state);
        break;
    case MODE_ST_BATT:
        if (s_batt_from_alarm)
        {
            rt_kprintf("[MODE] BATT: short ignored (alarm peek)\n");
            break;
        }
        if ((s_prev == MODE_ST_FORCE_OFF) || bat_is_protect())
        {
            rt_kprintf("[MODE] BATT: ON blocked (force/protect)\n");
            break;
        }
        batt_disarm();
        s_state = MODE_ST_ON;
        rt_kprintf("[MODE] BATT -> ON (short)\n");
        on_led_rtc_begin();
#if USE_ADC_BAT
        /* 看电已采过：若已是预警，边沿在 BATT 里可能已进 LOW_BATT；此处补进 ON 后的预警 */
        if (adc_bat_get_level() == ADC_BAT_LVL_WARN)
        {
            handle_bat_warn();
            break;
        }
#endif
        session_on_start();
        break;
    case MODE_ST_ON:
    case MODE_ST_LOW_BATT:
        rt_kprintf("[MODE] %s -> OFF (short)\n", mode_name(s_state));
        enter_off_from_on();
        break;
    case MODE_ST_ALARM:
        peek_batt_from_alarm();
        break;
    case MODE_ST_FAKE_OFF:
        if (s_fake_resume == MODE_ST_ALARM)
        {
            peek_batt_from_alarm();
        }
        else
        {
            rt_kprintf("[MODE] FAKE_OFF(ON) -> OFF (short)\n");
            enter_off_from_on();
        }
        break;
    case MODE_ST_PASSTHRU:
        rt_kprintf("[MODE] SOS_SHORT ignored (passthru)\n");
        break;
    default:
        break;
    }
}

static void handle_sos_long(void)
{
    if (s_state == MODE_ST_PASSTHRU)
    {
        rt_kprintf("[MODE] SOS_LONG ignored (exit passthru first)\n");
        return;
    }
    if (logic_is_alarm())
    {
        exit_alarm();
        return;
    }
    if ((s_state == MODE_ST_FORCE_OFF) || bat_is_protect())
    {
        rt_kprintf("[MODE] SOS_LONG ignored (force/protect)\n");
        return;
    }
    enter_alarm(alarm_from_state());
}

static void handle_fall(void)
{
    if (s_state == MODE_ST_PASSTHRU)
    {
        rt_kprintf("[MODE] FALL ignored (exit passthru first)\n");
        return;
    }
    if (logic_is_alarm())
    {
        if ((s_state == MODE_ST_BATT) && s_batt_from_alarm)
        {
            batt_disarm();
            s_batt_from_alarm = 0;
            s_state = MODE_ST_ALARM;
            rt_kprintf("[MODE] BATT -> ALARM (fall, keep session)\n");
        }
        return;
    }
    if ((s_state == MODE_ST_FORCE_OFF) || bat_is_protect())
    {
        rt_kprintf("[MODE] FALL ignored (force/protect)\n");
        return;
    }
    enter_alarm(alarm_from_state());
}

static void handle_bat_warn(void)
{
    /* 告警/充电/透传/保护不进。OFF / 关机看电 / ON / FAKE_OFF←ON 发 1 条 N */
    if (s_lb_sent)
    {
        /* 本轮 WARN 已发过；STOP2 复位后电量仍 WARN 不再发 */
        return;
    }
    if (usb_is_present() || (s_state == MODE_ST_CHARGE) ||
        (s_state == MODE_ST_PASSTHRU) || (s_state == MODE_ST_FORCE_OFF) ||
        (s_state == MODE_ST_LOW_BATT))
    {
        return;
    }
    if (logic_is_alarm())
    {
        return;
    }
    if (bat_is_protect())
    {
        return;
    }
    if (s_state == MODE_ST_FAKE_OFF)
    {
        if (s_fake_resume != MODE_ST_ON)
        {
            return;
        }
        hb_stop();
        s_lb_resume = MODE_ST_ON;
    }
    else if (s_state == MODE_ST_BATT)
    {
        /* 看电 5s 内不打断灯；超时回 OFF 后 handle_batt_to 再补 LOW_BATT */
        return;
    }
    else if ((s_state == MODE_ST_OFF) || (s_state == MODE_ST_ON))
    {
        s_lb_resume = s_state;
        nosim_hint_disarm();
    }
    else
    {
        return;
    }
    s_state = MODE_ST_LOW_BATT;
    s_lb_sent = 1;
    sticky_persist_lb_sent();
    iwdg_for_alive();
    rt_kprintf("[MODE] -> LOW_BATT (from %s)\n", mode_name(s_lb_resume));
    session_lowbatt_once();
}

static void handle_bat_ok(void)
{
    if (!s_lb_sent)
    {
        return;
    }
    s_lb_sent = 0;
    sticky_persist_lb_sent();
    rt_kprintf("[MODE] bat OK, LOW_BATT latch cleared\n");
}

static void handle_low_batt_done(void)
{
    mode_state_t back;

    if (s_state != MODE_ST_LOW_BATT)
    {
        return;
    }
    if (usb_is_present())
    {
        s_prev = (s_lb_resume == MODE_ST_OFF) ? MODE_ST_OFF : MODE_ST_ON;
        enter_charge(s_prev);
        return;
    }
    if (bat_is_protect())
    {
        enter_force_off();
        return;
    }
    back = s_lb_resume;
    s_state = back;
    rt_kprintf("[MODE] LOW_BATT -> %s (done)\n", mode_name(s_state));
    if (s_state == MODE_ST_ON)
    {
        on_led_rtc_begin();
        session_on_start();
    }
    else if (s_state == MODE_ST_OFF)
    {
        sticky_commit(MODE_ST_OFF, 0, 0);
    }
}

static void handle_usb_in(void)
{
    mode_state_t under;

#if USE_USB_CDC
    usb_cdc_start();
#endif
    if (s_state == MODE_ST_PASSTHRU)
    {
        /* 保持透传；LED 由 mode_ui_sync 显示 CHARGE */
        rt_kprintf("[MODE] PASSTHRU: USB in (LED=CHARGE view)\n");
        return;
    }
    if (s_state == MODE_ST_CHARGE)
    {
        return;
    }
    if (logic_is_alarm())
    {
        /* 告警中 USB 不退出；假关机则醒到 ALARM，有 USB 不再进 FAKE_OFF */
        if (s_state == MODE_ST_FAKE_OFF)
        {
            hb_stop();
            s_state = MODE_ST_ALARM;
            rt_kprintf("[MODE] FAKE_OFF -> ALARM (usb, keep alarm)\n");
        }
        return;
    }
    if (s_state == MODE_ST_LOW_BATT)
    {
        under = (s_lb_resume == MODE_ST_OFF) ? MODE_ST_OFF : MODE_ST_ON;
    }
    else if (s_state == MODE_ST_FAKE_OFF)
    {
        under = s_fake_resume;
    }
    else
    {
        under = (s_state == MODE_ST_BATT) ? s_prev : s_state;
    }
    enter_charge(under);
}

static void handle_usb_out(void)
{
#if USE_USB_CDC
    usb_cdc_stop();
#endif
    if (s_state == MODE_ST_PASSTHRU)
    {
        /* 仍透传；若 resume 曾是 CHARGE，退出时回到 s_prev */
        if (s_pt_resume == MODE_ST_CHARGE)
        {
            s_pt_resume = s_prev;
        }
        rt_kprintf("[MODE] PASSTHRU: USB out (resume=%s)\n", mode_name(s_pt_resume));
        return;
    }
    if (logic_is_alarm())
    {
        return;
    }
    if (s_state != MODE_ST_CHARGE)
    {
        return;
    }
    s_state = s_prev;
    rt_kprintf("[MODE] CHARGE -> %s (usb out)\n", mode_name(s_state));
    adc_on_charge_leave();
    if (s_state == MODE_ST_ON)
    {
        on_led_rtc_begin();
        session_on_start();
    }
    if (s_state == MODE_ST_OFF)
    {
        sticky_commit(MODE_ST_OFF, 0, 0);
    }
    if (bat_is_protect())
    {
        enter_force_off();
    }
}

static void handle_batt_to(void)
{
    if (s_state != MODE_ST_BATT)
    {
        return;
    }
    batt_disarm();
    s_state = s_prev;
    if (s_batt_from_alarm)
    {
        s_batt_from_alarm = 0;
        s_state = MODE_ST_ALARM;
    }
    rt_kprintf("[MODE] BATT -> %s (timeout)\n", mode_name(s_state));
    if ((s_state != MODE_ST_FORCE_OFF) && bat_is_protect())
    {
        enter_force_off();
        return;
    }
    if ((s_state == MODE_ST_OFF) || (s_state == MODE_ST_FORCE_OFF))
    {
        hw_off_rails_led();
    }
#if USE_ADC_BAT
    /* 看电窗口内采到预警、超时才回到 OFF：补进 LOW_BATT 发 1 条 */
    if ((s_state == MODE_ST_OFF) && (adc_bat_get_level() == ADC_BAT_LVL_WARN))
    {
        handle_bat_warn();
    }
#endif
}

static void handle_bat_protect(void)
{
    enter_force_off();
}

static void handle_rtc_wu(void)
{
    /* 只在 FAKE_OFF 空载采 1 次；CHARGE 走 15s 周期，OFF 不采。
     * 不在此读缓存判保护：等 ADC 线程边沿投 BAT_WARN / BAT_PROTECT。 */
    if (s_state != MODE_ST_FAKE_OFF)
    {
        return;
    }
#if USE_ADC_BAT
    adc_bat_request();
#endif
}

/** 假关机 ON：插卡开 10s 闪；拔卡无灯可亮，直接真关机。提示窗口内插卡则取消关机。 */
static void handle_sim(void)
{
    if ((s_state == MODE_ST_ON) && s_nosim_hint)
    {
        if (sim_present())
        {
            nosim_hint_disarm();
            on_led_rtc_begin();
            enter_fake_off();
            rt_kprintf("[MODE] ON no-SIM hint: SIM in -> 10s LED\n");
        }
        return;
    }
    if ((s_state != MODE_ST_FAKE_OFF) || (s_fake_resume != MODE_ST_ON))
    {
        return;
    }
    if (sim_present())
    {
#if USE_LED
        led_dark_unlock();
        led_wait_rtc_hb(1);
        s_ui_view = (mode_state_t)0xFF;
#endif
        rt_kprintf("[MODE] FAKE_OFF(ON) SIM in -> 10s LED\n");
    }
    else
    {
        rt_kprintf("[MODE] FAKE_OFF(ON) SIM out -> OFF\n");
        enter_off_from_on();
    }
}

static void mode_dispatch(rt_uint32_t set)
{
    if (set & MODE_EVT_BAT_PROTECT)
    {
        handle_bat_protect();
    }
    if (set & MODE_EVT_BAT_WARN)
    {
        handle_bat_warn();
    }
    if (set & MODE_EVT_BAT_OK)
    {
        handle_bat_ok();
    }
    if (set & MODE_EVT_LOW_BATT_DONE)
    {
        handle_low_batt_done();
    }
    if (set & MODE_EVT_PT_APPLY)
    {
        handle_pt_apply();
    }
    if (set & MODE_EVT_SOS_SHORT)
    {
        handle_sos_short();
    }
    if (set & MODE_EVT_SOS_LONG)
    {
        handle_sos_long();
    }
    if (set & MODE_EVT_FALL)
    {
        handle_fall();
    }
    if (set & MODE_EVT_USB_IN)
    {
        handle_usb_in();
    }
    if (set & MODE_EVT_USB_OUT)
    {
        handle_usb_out();
    }
    if (set & MODE_EVT_BATT_TO)
    {
        handle_batt_to();
    }
    if (set & MODE_EVT_SHOT_BUSY)
    {
        handle_shot_busy();
    }
    if (set & MODE_EVT_SHOT_IDLE)
    {
        handle_shot_idle();
    }
    if (set & MODE_EVT_RTC_WU)
    {
        handle_rtc_wu();
    }
    if (set & MODE_EVT_SIM)
    {
        handle_sim();
    }
}

static void mode_thread_entry(void *param)
{
    rt_uint32_t set;
    rt_int32_t wait;
    const rt_uint32_t mask = MODE_EVT_SOS_SHORT | MODE_EVT_SOS_LONG | MODE_EVT_FALL |
                             MODE_EVT_USB_IN | MODE_EVT_USB_OUT | MODE_EVT_BATT_TO |
                             MODE_EVT_BAT_PROTECT | MODE_EVT_BAT_WARN | MODE_EVT_BAT_OK |
                             MODE_EVT_PT_APPLY | MODE_EVT_LOW_BATT_DONE | MODE_EVT_SIM |
                             MODE_EVT_SHOT_BUSY | MODE_EVT_SHOT_IDLE | MODE_EVT_RTC_WU;

    (void)param;

#if USE_LED
    /* INIT_APP 时 LED 可能还没 ready，消息会丢；线程起来后一定能投递 */
    s_ui_view = (mode_state_t)0xFF;
    mode_ui_sync();
#endif
    /* 看电 5s 从线程起来算，避免 INIT 里 arm 的截止时间过早把灯掐掉 */
    if (s_batt_armed && (s_state == MODE_ST_BATT))
    {
        batt_arm();
    }

    while (1)
    {
        pm_run_sync();
        iwdg_for_alive();
        maybe_stop2();
        if (s_batt_armed && (s_state == MODE_ST_BATT))
        {
            rt_tick_t now = rt_tick_get();
            if ((rt_int32_t)(s_batt_deadline - now) <= 0)
            {
                handle_batt_to();
                mode_ui_sync();
                wait = RT_WAITING_FOREVER;
            }
            else
            {
                wait = (rt_int32_t)(s_batt_deadline - now);
            }
        }
        else if (s_nosim_hint && (s_state == MODE_ST_ON))
        {
            rt_tick_t now = rt_tick_get();
            if ((rt_int32_t)(s_nosim_hint_deadline - now) <= 0)
            {
                handle_nosim_hint_to();
                mode_ui_sync();
                pm_run_sync();
                maybe_stop2();
                wait = RT_WAITING_FOREVER;
            }
            else
            {
                wait = (rt_int32_t)(s_nosim_hint_deadline - now);
            }
        }
        else
        {
            rt_int32_t defer = stop2_defer_ticks();

            /* 还不能睡就限时等，到点回来重新判断，别把自己挂死在这 */
            wait = (defer > 0) ? defer : RT_WAITING_FOREVER;
        }
        /* CHARGE/ON 已开狗：不能 FOREVER 挂着，否则 ≈26s 复位把 USB COM 掐掉 */
        if (iwdg_should_run())
        {
            rt_int32_t feed = (rt_int32_t)rt_tick_from_millisecond(MODE_IWDG_FEED_MS);

            if ((wait < 0) || (wait > feed))
            {
                wait = feed;
            }
        }

        set = 0;
        if (rt_event_recv(&s_mode_evt, mask,
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          wait,
                          &set) == RT_EOK)
        {
            mode_dispatch(set);
            mode_ui_sync();
            pm_run_sync();
            iwdg_for_alive();
            maybe_stop2();
        }
        else if (s_batt_armed && (s_state == MODE_ST_BATT) &&
                 ((rt_int32_t)(s_batt_deadline - rt_tick_get()) <= 0))
        {
            handle_batt_to();
            mode_ui_sync();
            pm_run_sync();
            maybe_stop2();
        }
        else if (s_nosim_hint && (s_state == MODE_ST_ON) &&
                 ((rt_int32_t)(s_nosim_hint_deadline - rt_tick_get()) <= 0))
        {
            handle_nosim_hint_to();
            mode_ui_sync();
            pm_run_sync();
            maybe_stop2();
        }
        else
        {
            iwdg_for_alive();
            maybe_stop2();
        }
    }
}

int mode_init(void)
{
    rt_err_t err;

    s_state = MODE_ST_OFF;
    s_prev = MODE_ST_OFF;
    s_pt_resume = MODE_ST_OFF;
    s_fake_resume = MODE_ST_OFF;
    s_batt_from_alarm = 0;
    s_alarm_unix = 0;
    s_nosim_hint = 0;
    s_lb_sent = 0;
    s_pt_flags = 0;
    s_pt_req_flags = 0;
    batt_disarm();
    s_ready = 0;
    s_evt_ok = 0;
    s_evt_pend = 0;
    pm_run_sync(); /* OFF 起就禁止 STOP0，等 FAKE_OFF 再放行 */

    rt_event_init(&s_mode_evt, "mode", RT_IPC_FLAG_FIFO);
    s_evt_ok = 1;
    if (s_evt_pend != 0)
    {
        rt_event_send(&s_mode_evt, s_evt_pend);
        s_evt_pend = 0;
    }

    err = rt_thread_init(&s_mode_thread,
                         "mode",
                         mode_thread_entry,
                         RT_NULL,
                         s_mode_stack,
                         sizeof(s_mode_stack),
                         MODE_THREAD_PRIO,
                         20);
    if (err != RT_EOK)
    {
        rt_kprintf("[MODE] thread init failed\n");
        return -1;
    }

#if USE_RTC
#if USE_LED
    rtc_wu_hook_set(RTC_WU_SLOT_LED, led_on_rtc_wu);
#endif
    rtc_wu_hook_set(RTC_WU_SLOT_SESS, session_on_rtc_wu);
    rtc_wu_hook_set(RTC_WU_SLOT_MODE, mode_on_rtc_wu);
#endif

    (void)bkp_user_init();
    rt_pin_mode(USB_IN_RT_PIN, PIN_MODE_INPUT);
    /* 先读 BKP：lb_sent 必须在 STOP2 复位后仍在，再决定是否发 LOW_BATT */
    if (sticky_try_restore())
    {
        /* ALARM 已恢复 */
    }
    if (bat_is_protect() && !usb_is_present())
    {
        enter_force_off();
    }
    else if (s_state == MODE_ST_ALARM)
    {
#if USE_USB_CDC
        if (usb_is_present())
        {
            usb_cdc_start();
        }
#endif
    }
    else if (usb_is_present())
    {
        enter_charge(MODE_ST_OFF);
    }
#if USE_ADC_BAT
    else if (bat_is_protect())
    {
        enter_force_off();
    }
#endif
    else if (board_boot_fall_active())
    {
        enter_alarm(MODE_ST_OFF);
    }

    if (((s_state == MODE_ST_OFF) || (s_state == MODE_ST_FORCE_OFF)) &&
        (board_boot_from_stop2() || board_boot_sos_down()) &&
        !board_boot_fall_active())
    {
        enter_batt_peek(s_state);
    }

#if USE_ADC_BAT
    if (!usb_is_present())
    {
        if (adc_bat_get_level() == ADC_BAT_LVL_OK)
        {
            /* 上电空载已 OK：ADC 无边沿，这里清闩，下一轮 WARN 才能再发 */
            handle_bat_ok();
        }
        else if ((s_state == MODE_ST_OFF) &&
                 (adc_bat_get_level() == ADC_BAT_LVL_WARN))
        {
            /* 冷启动 OFF 且已 WARN：发 1 条；lb_sent 挡住 STOP2 后再发 */
            handle_bat_warn();
        }
    }
#endif

    s_ready = 1;
    rt_thread_startup(&s_mode_thread);

    mode_ui_sync();
    if ((s_state == MODE_ST_OFF) || (s_state == MODE_ST_FORCE_OFF))
    {
        hw_off_rails_led();
        sticky_commit((s_state == MODE_ST_FORCE_OFF) ? MODE_ST_FORCE_OFF : MODE_ST_OFF,
                      0, 0);
    }
    iwdg_for_alive();
    pm_run_sync();
    rt_kprintf("[MODE] ready state=%s lb_sent=%u\n",
               mode_name(s_state), (unsigned)s_lb_sent);
    return 0;
}

static int app_mode_init(void)
{
    return mode_init();
}
INIT_APP_EXPORT(app_mode_init);
