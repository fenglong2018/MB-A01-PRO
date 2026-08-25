/**
 * @file session_alarm.c
 * @brief 会话：ALARM 2/5/10min（满 72h 仍 A）；ON 10min；LOW_BATT 单次
 *        一拍结束通知 MODE 可进 FAKE_OFF
 */
#include <rtthread.h>
#include <string.h>

#include "session_alarm.h"
#include "config.h"
#include "mode.h"
#include "key.h"
#include "cfg.h"
#if USE_RTC
#include "rtc.h"
#endif
#if USE_PM
#include "pm.h"
#endif
#include "iwdg.h"

#if USE_GNSS
#include "gnss.h"
#include "msg_pack.h"
#endif
#if USE_RDSS
#include "rdss.h"
#endif
#if USE_ADC_BAT
#include "adc_bat.h"
#endif

#ifndef SESSION_THREAD_STACK
#define SESSION_THREAD_STACK    1536
#endif
#ifndef SESSION_THREAD_PRIO
#define SESSION_THREAD_PRIO     18
#endif

#define SESSION_S_24H           (24u * 60u * 60u)
#define SESSION_S_48H           (48u * 60u * 60u)

#define SESSION_CMD_STOP        1u
#define SESSION_CMD_TICK        2u
#define SESSION_CMD_START_ALARM 3u
#define SESSION_CMD_START_ON    4u
#define SESSION_CMD_START_LB    5u
#define SESSION_CMD_START_TEST  6u
#define SESSION_CMD_RESUME_ALARM 7u
#define SESSION_CMD_WU          8u

#define SESSION_MS_2MIN         (2u * 60u * 1000u)
#define SESSION_MS_5MIN         (5u * 60u * 1000u)
#define SESSION_MS_10MIN        (10u * 60u * 1000u)

typedef enum
{
    SESS_KIND_NONE = 0,
    SESS_KIND_ALARM,
    SESS_KIND_ON,
    SESS_KIND_LOWBATT,
} sess_kind_t;

typedef struct
{
    uint8_t cmd;
    uint32_t unix_sec;
} session_cmd_t;

static struct rt_thread s_thread;
static rt_uint8_t s_stack[SESSION_THREAD_STACK];
static struct rt_messagequeue s_cmd_mq;
static rt_uint8_t s_cmd_pool[8 * sizeof(session_cmd_t)];
static struct rt_timer s_timer;
static sess_kind_t s_kind;
static uint32_t s_alarm_start;
static uint8_t s_inited;
static uint8_t s_on_rtc_done;
static uint32_t s_next_unix;
static uint16_t s_wu_left;
static uint8_t s_last_nosim;

static void session_post(uint8_t c, uint32_t unix_sec);
static void radio_abort(void);

static void session_timer_cb(void *param)
{
    session_cmd_t cmd;

    (void)param;
    cmd.cmd = SESSION_CMD_TICK;
    cmd.unix_sec = 0;
    (void)rt_mq_send(&s_cmd_mq, &cmd, sizeof(cmd));
}

static int mq_recv_feed(void *mq, void *buf, rt_size_t size, uint32_t timeout_ms)
{
    uint32_t left = timeout_ms;

    while (left > 0u)
    {
        uint32_t slice = (left > 5000u) ? 5000u : left;
        iwdg_feed();
        if (rt_mq_recv(mq, buf, size, rt_tick_from_millisecond(slice)) == RT_EOK)
        {
            return 0;
        }
        left -= slice;
    }
    return -1;
}
static void radio_abort(void)
{
#if USE_GNSS
    gnss_on_bat_protect();
#endif
#if USE_RDSS
    rdss_on_bat_protect();
#endif
}

static void arm_timer_ms(rt_uint32_t period_ms)
{
    rt_tick_t ticks = rt_tick_from_millisecond(period_ms);

    rt_timer_control(&s_timer, RT_TIMER_CTRL_SET_TIME, &ticks);
    rt_timer_start(&s_timer);
}

static uint32_t unix_now(void)
{
#if USE_RTC
    return rtc_get_unix();
#else
    return 0;
#endif
}

static void stamp_next_s(uint32_t period_s)
{
    uint32_t now = unix_now();

    s_next_unix = ((now != 0u) && (period_s != 0u)) ? (now + period_s) : 0u;
    s_wu_left = (uint16_t)((period_s + 9u) / 10u);
}

static void arm_next_timer(void)
{
    uint32_t now;
    uint32_t elapsed;

    if (s_kind == SESS_KIND_ON)
    {
        stamp_next_s(10u * 60u);
        arm_timer_ms(SESSION_MS_10MIN);
        return;
    }

    if (s_kind != SESS_KIND_ALARM)
    {
        return;
    }

    now = unix_now();
    if ((s_alarm_start == 0) || (now == 0) || (now < s_alarm_start))
    {
        elapsed = 0;
    }
    else
    {
        elapsed = now - s_alarm_start;
    }
    if (elapsed < SESSION_S_24H)
    {
        stamp_next_s(2u * 60u);
        arm_timer_ms(SESSION_MS_2MIN);
    }
    else if (elapsed < SESSION_S_48H)
    {
        stamp_next_s(5u * 60u);
        arm_timer_ms(SESSION_MS_5MIN);
    }
    else
    {
        /* 48h 起含满 72h：仍 10min A，不切 ON */
        stamp_next_s(10u * 60u);
        arm_timer_ms(SESSION_MS_10MIN);
    }
}

static void maybe_rtc_from_gnss(uint32_t unix_sec)
{
#if USE_RTC
    if (unix_sec == 0)
    {
        return;
    }
    if (s_kind == SESS_KIND_ON)
    {
        if (!s_on_rtc_done)
        {
            if (rtc_post_unix(unix_sec, RTC_SRC_GNSS) == RT_EOK)
            {
                s_on_rtc_done = 1;
                rt_kprintf("[SESSION] RTC sync ON unix=%lu\n",
                           (unsigned long)unix_sec);
            }
        }
        return;
    }
    if (s_kind == SESS_KIND_ALARM)
    {
        if (!rtc_is_synced())
        {
            if (rtc_post_unix(unix_sec, RTC_SRC_GNSS) == RT_EOK)
            {
                rt_kprintf("[SESSION] RTC sync ALARM unix=%lu\n",
                           (unsigned long)unix_sec);
            }
        }
        if (s_alarm_start == 0)
        {
            s_alarm_start = unix_sec;
            mode_alarm_anchor_latch(unix_sec);
        }
    }
#else
    (void)unix_sec;
#endif
}

static void run_one_cycle(uint8_t alarm_mode)
{
#if USE_GNSS
    gnss_msg_t gmsg;
    uint8_t pack[MSG_PACK_MAX];
    uint16_t plen;
#else
    (void)alarm_mode;
#endif
#if USE_RDSS
    rdss_msg_t rmsg;
#endif

    s_last_nosim = 0;
    if (!sim_present())
    {
        s_last_nosim = 1;
        rt_kprintf("[SESSION] no SIM card, skip GNSS/RDSS\n");
        return;
    }

#if USE_ADC_BAT
    if (adc_bat_sample_wait(ADC_BAT_SAMPLE_WAIT_MS) != 0)
    {
        rt_kprintf("[SESSION] adc wait fail, use cache\n");
    }
    if (adc_bat_get_level() == ADC_BAT_LVL_PROTECT)
    {
        rt_kprintf("[SESSION] bat protect, skip GNSS/RDSS\n");
        return;
    }
#endif

#if !USE_GNSS
    rt_kprintf("[SESSION] GNSS not built, skip cycle\n");
    return;
#else
    if (gnss_start_fix() != RT_EOK)
    {
        rt_kprintf("[SESSION] gnss_start_fix fail\n");
        return;
    }

    if (mq_recv_feed(gnss_result_mq(), &gmsg, sizeof(gmsg),
                     GNSS_FIX_TIMEOUT_MS + 5000u) != 0)
    {
        rt_kprintf("[SESSION] gnss result timeout\n");
        return;
    }
    if (!gmsg.ok || !gmsg.fix.valid)
    {
        rt_kprintf("[SESSION] gnss fail reason=%u, skip RDSS\n", gmsg.reason);
        return;
    }
#if USE_RTC
    if (cfg_get_first_fix_unix() == 0)
    {
        cfg_set_first_fix_unix(gmsg.fix.unix_sec);
    }
    maybe_rtc_from_gnss(gmsg.fix.unix_sec);
#endif

#if USE_RDSS
    if (rdss_get_card_id() == 0)
    {
        if (rdss_ensure_card(RDSS_CARD_TIMEOUT_MS) != RT_EOK)
        {
            rt_kprintf("[SESSION] RDSS card query fail, use cfg device_id\n");
        }
    }
#endif

    plen = msg_pack_loca_up(pack, sizeof(pack), &gmsg.fix, alarm_mode);
    if (plen == 0)
    {
        rt_kprintf("[SESSION] pack fail\n");
        return;
    }
    rt_kprintf("[SESSION] pack len=%u mode=%c\n",
               (unsigned)plen, alarm_mode ? 'A' : 'N');

#if USE_RDSS
    if (rdss_start_send(pack, plen) != RT_EOK)
    {
        rt_kprintf("[SESSION] rdss_start_send fail\n");
        return;
    }
    if (mq_recv_feed(rdss_result_mq(), &rmsg, sizeof(rmsg),
                     RDSS_BEAM_TIMEOUT_MS + RDSS_FKI_TIMEOUT_MS + 5000u) != 0)
    {
        rt_kprintf("[SESSION] rdss result timeout\n");
        return;
    }
    rt_kprintf("[SESSION] rdss ok=%u reason=%u\n", rmsg.ok, rmsg.reason);
#else
    rt_kprintf("[SESSION] RDSS not built, pack only\n");
#endif
#endif /* USE_GNSS */
}

static void cycle_and_idle(uint8_t alarm_mode)
{
#if USE_PM
    pm_lock();
#endif
    iwdg_feed();
    mode_post_event(MODE_EVT_SHOT_BUSY);
    run_one_cycle(alarm_mode);
    if ((s_kind == SESS_KIND_ALARM) || (s_kind == SESS_KIND_ON))
    {
        mode_post_event(MODE_EVT_SHOT_IDLE);
    }
#if USE_PM
    pm_unlock();
#endif
}

static void do_stop(void)
{
    s_kind = SESS_KIND_NONE;
    rt_timer_stop(&s_timer);
    radio_abort();
    rt_kprintf("[SESSION] stop\n");
}

static void session_thread_entry(void *param)
{
    session_cmd_t cmd;

    (void)param;
    while (1)
    {
        if (rt_mq_recv(&s_cmd_mq, &cmd, sizeof(cmd), RT_WAITING_FOREVER) != RT_EOK)
        {
            continue;
        }

        switch (cmd.cmd)
        {
        case SESSION_CMD_STOP:
            do_stop();
            break;

        case SESSION_CMD_START_ALARM:
        case SESSION_CMD_RESUME_ALARM:
            rt_timer_stop(&s_timer);
            radio_abort();
            s_kind = SESS_KIND_ALARM;
            s_alarm_start = cmd.unix_sec;
            rt_kprintf("[SESSION] alarm %s unix=%lu\n",
                       (cmd.cmd == SESSION_CMD_RESUME_ALARM) ? "resume" : "start",
                       (unsigned long)s_alarm_start);
            cycle_and_idle(1);
            if (s_kind == SESS_KIND_ALARM)
            {
                arm_next_timer();
            }
            break;

        case SESSION_CMD_START_ON:
            rt_timer_stop(&s_timer);
            radio_abort();
            s_kind = SESS_KIND_ON;
            s_on_rtc_done = 0;
            rt_kprintf("[SESSION] on start (10min)\n");
            cycle_and_idle(0);
            if (s_kind == SESS_KIND_ON)
            {
                arm_next_timer();
            }
            break;

        case SESSION_CMD_START_LB:
            rt_timer_stop(&s_timer);
            radio_abort();
            s_kind = SESS_KIND_LOWBATT;
            rt_kprintf("[SESSION] lowbatt once\n");
            run_one_cycle(0);
            s_kind = SESS_KIND_NONE;
            mode_post_event(MODE_EVT_LOW_BATT_DONE);
            break;

        case SESSION_CMD_START_TEST:
            rt_timer_stop(&s_timer);
            radio_abort();
            s_kind = SESS_KIND_NONE;
            rt_kprintf("[SESSION] test once\n");
            run_one_cycle(0);
            break;

        case SESSION_CMD_TICK:
            if (s_kind == SESS_KIND_ALARM)
            {
                cycle_and_idle(1);
                if (s_kind == SESS_KIND_ALARM)
                {
                    arm_next_timer();
                }
            }
            else if (s_kind == SESS_KIND_ON)
            {
                cycle_and_idle(0);
                if (s_kind == SESS_KIND_ON)
                {
                    arm_next_timer();
                }
            }
            break;

        case SESSION_CMD_WU:
        {
            uint32_t now = unix_now();
            int due = 0;

            if ((s_kind != SESS_KIND_ALARM) && (s_kind != SESS_KIND_ON))
            {
                break;
            }
            if ((s_next_unix != 0u) && (now != 0u) && (now >= s_next_unix))
            {
                due = 1;
            }
            else if (s_next_unix == 0u)
            {
                if (s_wu_left > 0u)
                {
                    s_wu_left--;
                }
                if (s_wu_left == 0u)
                {
                    due = 1;
                }
            }
            if (due)
            {
                session_post(SESSION_CMD_TICK, 0);
            }
            break;
        }

        default:
            break;
        }
    }
}

static int session_ensure_init(void)
{
    rt_err_t err;

    if (s_inited)
    {
        return 0;
    }

    rt_mq_init(&s_cmd_mq, "sesscmd", s_cmd_pool, sizeof(session_cmd_t),
               sizeof(s_cmd_pool), RT_IPC_FLAG_FIFO);
    rt_timer_init(&s_timer, "sesstm", session_timer_cb, RT_NULL,
                  rt_tick_from_millisecond(SESSION_MS_2MIN),
                  RT_TIMER_FLAG_ONE_SHOT | RT_TIMER_FLAG_SOFT_TIMER);

    err = rt_thread_init(&s_thread, "session", session_thread_entry, RT_NULL,
                         s_stack, sizeof(s_stack), SESSION_THREAD_PRIO, 10);
    if (err != RT_EOK)
    {
        rt_kprintf("[SESSION] thread init fail\n");
        return -1;
    }
    rt_thread_startup(&s_thread);
    s_inited = 1;
    return 0;
}

static void session_post(uint8_t c, uint32_t unix_sec)
{
    session_cmd_t cmd;

    if (session_ensure_init() != 0)
    {
        return;
    }
    cmd.cmd = c;
    cmd.unix_sec = unix_sec;
    if (rt_mq_send(&s_cmd_mq, &cmd, sizeof(cmd)) != RT_EOK)
    {
        rt_kprintf("[SESSION] mq full cmd=%u\n", (unsigned)c);
    }
}

void session_alarm_start(void)
{
    session_post(SESSION_CMD_START_ALARM, unix_now());
}

void session_alarm_resume(uint32_t start_unix)
{
    session_post(SESSION_CMD_RESUME_ALARM, start_unix);
}

void session_alarm_stop(void)
{
    if (!s_inited)
    {
        return;
    }
    session_post(SESSION_CMD_STOP, 0);
}

void session_on_start(void)
{
    session_post(SESSION_CMD_START_ON, 0);
}

void session_on_stop(void)
{
    if (!s_inited)
    {
        return;
    }
    session_post(SESSION_CMD_STOP, 0);
}

void session_lowbatt_once(void)
{
    session_post(SESSION_CMD_START_LB, 0);
}

void session_stop_all(void)
{
    if (!s_inited)
    {
        return;
    }
    session_post(SESSION_CMD_STOP, 0);
}

void session_test_once(void)
{
    session_post(SESSION_CMD_START_TEST, 0);
}

void session_on_rtc_wu(void)
{
    session_cmd_t cmd;

    if (!s_inited)
    {
        return;
    }
    cmd.cmd = SESSION_CMD_WU;
    cmd.unix_sec = 0;
    (void)rt_mq_send(&s_cmd_mq, &cmd, sizeof(cmd));
}

int session_last_was_nosim(void)
{
    return s_last_nosim ? 1 : 0;
}
