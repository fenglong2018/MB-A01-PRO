/**
 * @file mode.c
 * @brief 整机模式状态机任务（规则见 fsm.md）
 */
#include <rtthread.h>
#include <rtdevice.h>
#include "config.h"
#include "mode.h"
#include "session_alarm.h"
#include "board_pins.h"
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

static struct rt_event s_mode_evt;
static struct rt_thread s_mode_thread;
static rt_uint8_t s_mode_stack[MODE_THREAD_STACK];

static mode_state_t s_state = MODE_ST_OFF;
static mode_state_t s_prev = MODE_ST_OFF;       /* CHARGE/BATT/ALARM 等底层返回 */
static mode_state_t s_pt_resume = MODE_ST_OFF;  /* 进入 PASSTHRU 前的 ON/CHARGE */
static uint8_t s_pt_flags;
static uint8_t s_pt_req_flags;
static rt_tick_t s_batt_deadline;
static rt_uint8_t s_batt_armed;
static rt_uint8_t s_ready;
#if USE_LED
static mode_state_t s_ui_view = (mode_state_t)0xFF;
#endif

static void passthru_radio_apply(uint8_t want);
static void passthru_radio_stop_all(void);
static int usb_is_present(void);

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
    return s_state;
}

#if USE_LED
static void mode_ui_sync(void)
{
    mode_state_t view = mode_led_view();

    if (s_ui_view == view)
    {
        return;
    }
    s_ui_view = view;
    led_post_mode((uint8_t)view);
}
#else
static void mode_ui_sync(void)
{
}
#endif

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
    if (!s_ready || (evt == 0))
    {
        return;
    }
    rt_event_send(&s_mode_evt, evt);
}

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
    /* 与 KEY 约定：低电平 = 插入 */
    return (rt_pin_read(USB_IN_RT_PIN) == PIN_LOW);
}

static int bat_is_protect(void)
{
#if USE_ADC_BAT
    return (adc_bat_get_level() == ADC_BAT_LVL_PROTECT);
#else
    return 0;
#endif
}

static void batt_arm(void)
{
    s_batt_armed = 1;
    s_batt_deadline = rt_tick_get() + rt_tick_from_millisecond(MODE_BATT_MS);
}

static void batt_disarm(void)
{
    s_batt_armed = 0;
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
    s_pt_resume = MODE_ST_OFF;

    if (usb_is_present())
    {
        if (resume != MODE_ST_CHARGE)
        {
            s_prev = resume; /* 多为 ON */
        }
        s_state = MODE_ST_CHARGE;
        rt_kprintf("[MODE] PASSTHRU -> CHARGE (usb)\n");
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
        s_pt_resume = MODE_ST_OFF;
        if (usb_is_present())
        {
            s_state = MODE_ST_CHARGE;
            s_prev = MODE_ST_OFF;
            rt_kprintf("[MODE] PASSTHRU -> CHARGE (protect+usb)\n");
            return;
        }
    }

    session_stop_all();

    batt_disarm();
    s_state = MODE_ST_FORCE_OFF;
    s_prev = MODE_ST_FORCE_OFF;
    rt_kprintf("[MODE] -> FORCE_OFF (bat protect)\n");
}

static void enter_alarm(mode_state_t from)
{
    if (s_state == MODE_ST_ALARM)
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
    /* LOW_BATT 为瞬时态，退出告警应回 ON 而非 LOW_BATT */
    if (from == MODE_ST_LOW_BATT)
    {
        from = MODE_ST_ON;
    }
    s_prev = from;
    session_stop_all();
    s_state = MODE_ST_ALARM;
    session_alarm_start();
    rt_kprintf("[MODE] -> ALARM (prev=%s)\n", mode_name(s_prev));
}

static void enter_charge(mode_state_t from_prev)
{
    if (s_state == MODE_ST_ALARM)
    {
        return;
    }
    if (s_state == MODE_ST_PASSTHRU)
    {
        /* 透传中插 USB：保持 PASSTHRU，仅刷新 LED */
        return;
    }
    batt_disarm();
    if (from_prev == MODE_ST_FORCE_OFF)
    {
        from_prev = MODE_ST_OFF;
    }
    session_stop_all();
    s_prev = from_prev;
    s_state = MODE_ST_CHARGE;
    rt_kprintf("[MODE] -> CHARGE (prev=%s)\n", mode_name(s_prev));
}

static void exit_alarm(void)
{
    mode_state_t back;

    if (s_state != MODE_ST_ALARM)
    {
        return;
    }
    session_stop_all();
    back = s_prev;
    if (usb_is_present())
    {
        s_state = MODE_ST_CHARGE;
        s_prev = back;
        rt_kprintf("[MODE] ALARM -> CHARGE (usb, prev=%s)\n", mode_name(s_prev));
    }
    else
    {
        s_state = back;
        rt_kprintf("[MODE] ALARM -> %s\n", mode_name(s_state));
        if (s_state == MODE_ST_ON)
        {
            session_on_start();
        }
        if (bat_is_protect())
        {
            enter_force_off();
        }
    }
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
    s_pt_resume = s_state;
    s_state = MODE_ST_PASSTHRU;
    s_pt_flags = 0;
    passthru_radio_apply(want);
    if (s_pt_flags == 0)
    {
        /* 全部通道失败：回退 */
        s_state = s_pt_resume;
        s_pt_resume = MODE_ST_OFF;
        rt_kprintf("[MODE] passthru enter failed, back %s\n", mode_name(s_state));
        return;
    }
    rt_kprintf("[MODE] %s -> PASSTHRU flags=0x%02x\n",
               mode_name(s_pt_resume), s_pt_flags);
}

static void handle_sos_short(void)
{
    switch (s_state)
    {
    case MODE_ST_OFF:
    case MODE_ST_FORCE_OFF:
        s_prev = s_state;
        s_state = MODE_ST_BATT;
        batt_arm();
        rt_kprintf("[MODE] %s -> BATT\n", mode_name(s_prev));
        break;
    case MODE_ST_BATT:
        if ((s_prev == MODE_ST_FORCE_OFF) || bat_is_protect())
        {
            rt_kprintf("[MODE] BATT: ON blocked (force/protect)\n");
            break;
        }
        batt_disarm();
        s_state = MODE_ST_ON;
        session_on_start();
        rt_kprintf("[MODE] BATT -> ON (short)\n");
        break;
    case MODE_ST_ON:
    case MODE_ST_LOW_BATT:
        rt_kprintf("[MODE] %s -> OFF (short)\n", mode_name(s_state));
        session_stop_all();
        s_state = MODE_ST_OFF;
        s_prev = MODE_ST_OFF;
        if (bat_is_protect())
        {
            enter_force_off();
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
    if (s_state == MODE_ST_ALARM)
    {
        exit_alarm();
        return;
    }
    if ((s_state == MODE_ST_FORCE_OFF) || bat_is_protect())
    {
        rt_kprintf("[MODE] SOS_LONG ignored (force/protect)\n");
        return;
    }
    enter_alarm(s_state);
}

static void handle_fall(void)
{
    if (s_state == MODE_ST_PASSTHRU)
    {
        rt_kprintf("[MODE] FALL ignored (exit passthru first)\n");
        return;
    }
    if (s_state == MODE_ST_ALARM)
    {
        return;
    }
    if ((s_state == MODE_ST_FORCE_OFF) || bat_is_protect())
    {
        rt_kprintf("[MODE] FALL ignored (force/protect)\n");
        return;
    }
    enter_alarm(s_state);
}

static void handle_bat_warn(void)
{
    /* 仅开机态进入低电单次上报；ALARM/CHARGE 等忽略 */
    if (s_state != MODE_ST_ON)
    {
        return;
    }
    if (bat_is_protect())
    {
        return;
    }
    s_state = MODE_ST_LOW_BATT;
    rt_kprintf("[MODE] ON -> LOW_BATT (warn)\n");
    session_lowbatt_once();
}

static void handle_low_batt_done(void)
{
    if (s_state != MODE_ST_LOW_BATT)
    {
        return;
    }
    if (usb_is_present())
    {
        s_prev = MODE_ST_ON;
        s_state = MODE_ST_CHARGE;
        rt_kprintf("[MODE] LOW_BATT -> CHARGE (usb)\n");
        return;
    }
    if (bat_is_protect())
    {
        enter_force_off();
        return;
    }
    s_state = MODE_ST_ON;
    session_on_start();
    rt_kprintf("[MODE] LOW_BATT -> ON (done)\n");
}

static void handle_usb_in(void)
{
    mode_state_t under;

    if (s_state == MODE_ST_PASSTHRU)
    {
        /* 保持透传；LED 由 mode_ui_sync 显示 CHARGE */
        rt_kprintf("[MODE] PASSTHRU: USB in (LED=CHARGE view)\n");
        return;
    }
    if ((s_state == MODE_ST_ALARM) || (s_state == MODE_ST_CHARGE))
    {
        return;
    }
    if (s_state == MODE_ST_LOW_BATT)
    {
        under = MODE_ST_ON;
    }
    else
    {
        under = (s_state == MODE_ST_BATT) ? s_prev : s_state;
    }
    enter_charge(under);
}

static void handle_usb_out(void)
{
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
    if (s_state == MODE_ST_ALARM)
    {
        return;
    }
    if (s_state != MODE_ST_CHARGE)
    {
        return;
    }
    s_state = s_prev;
    rt_kprintf("[MODE] CHARGE -> %s (usb out)\n", mode_name(s_state));
    if (s_state == MODE_ST_ON)
    {
        session_on_start();
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
    rt_kprintf("[MODE] BATT -> %s (timeout)\n", mode_name(s_state));
    if ((s_state != MODE_ST_FORCE_OFF) && bat_is_protect())
    {
        enter_force_off();
    }
}

static void handle_bat_protect(void)
{
    enter_force_off();
}

static void handle_alarm_expire(void)
{
    if (s_state != MODE_ST_ALARM)
    {
        return;
    }
    session_stop_all();
    batt_disarm();
    s_state = MODE_ST_ON;
    session_on_start();
    rt_kprintf("[MODE] ALARM -> ON (48h)\n");
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
    if (set & MODE_EVT_ALARM_EXPIRE)
    {
        handle_alarm_expire();
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
}

static void mode_thread_entry(void *param)
{
    rt_uint32_t set;
    rt_int32_t wait;
    const rt_uint32_t mask = MODE_EVT_SOS_SHORT | MODE_EVT_SOS_LONG | MODE_EVT_FALL |
                             MODE_EVT_USB_IN | MODE_EVT_USB_OUT | MODE_EVT_BATT_TO |
                             MODE_EVT_BAT_PROTECT | MODE_EVT_BAT_WARN | MODE_EVT_PT_APPLY |
                             MODE_EVT_ALARM_EXPIRE | MODE_EVT_LOW_BATT_DONE;

    (void)param;

    while (1)
    {
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
        else
        {
            wait = RT_WAITING_FOREVER;
        }

        set = 0;
        if (rt_event_recv(&s_mode_evt, mask,
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          wait,
                          &set) == RT_EOK)
        {
            mode_dispatch(set);
            mode_ui_sync();
        }
        else if (s_batt_armed && (s_state == MODE_ST_BATT) &&
                 ((rt_int32_t)(s_batt_deadline - rt_tick_get()) <= 0))
        {
            handle_batt_to();
            mode_ui_sync();
        }
    }
}

int mode_init(void)
{
    rt_err_t err;

    s_state = MODE_ST_OFF;
    s_prev = MODE_ST_OFF;
    s_pt_resume = MODE_ST_OFF;
    s_pt_flags = 0;
    s_pt_req_flags = 0;
    batt_disarm();
    s_ready = 0;

    rt_event_init(&s_mode_evt, "mode", RT_IPC_FLAG_FIFO);

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
    rt_thread_startup(&s_mode_thread);
    s_ready = 1;

    rt_pin_mode(USB_IN_RT_PIN, PIN_MODE_INPUT);
    if (usb_is_present())
    {
        enter_charge(MODE_ST_OFF);
    }
#if USE_ADC_BAT
    else if (bat_is_protect())
    {
        enter_force_off();
    }
#endif

    mode_ui_sync();
    rt_kprintf("[MODE] ready state=%s\n", mode_name(s_state));
    return 0;
}

static int app_mode_init(void)
{
    return mode_init();
}
INIT_APP_EXPORT(app_mode_init);
