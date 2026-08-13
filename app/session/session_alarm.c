/**
 * @file session_alarm.c
 * @brief 会话：有卡门控 → GNSS → 组包 → RDSS
 *        ALARM 2/5min（48h→ON）；ON 10min；LOW_BATT 单次
 */
#include <rtthread.h>
#include <string.h>

#include "session_alarm.h"
#include "config.h"
#include "mode.h"
#include "key.h"

#if USE_GNSS
#include "gnss.h"
#include "msg_pack.h"
#endif
#if USE_RDSS
#include "rdss.h"
#endif

#ifndef SESSION_THREAD_STACK
#define SESSION_THREAD_STACK    1536
#endif
#ifndef SESSION_THREAD_PRIO
#define SESSION_THREAD_PRIO     18
#endif

#define SESSION_CMD_STOP        1u
#define SESSION_CMD_TICK        2u
#define SESSION_CMD_START_ALARM 3u
#define SESSION_CMD_START_ON    4u
#define SESSION_CMD_START_LB    5u
#define SESSION_CMD_START_TEST  6u

#define SESSION_MS_2MIN         (2u * 60u * 1000u)
#define SESSION_MS_5MIN         (5u * 60u * 1000u)
#define SESSION_MS_10MIN        (10u * 60u * 1000u)
#define SESSION_MS_24H          (24u * 60u * 60u * 1000u)
#define SESSION_MS_48H          (48u * 60u * 60u * 1000u)

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
} session_cmd_t;

static struct rt_thread s_thread;
static rt_uint8_t s_stack[SESSION_THREAD_STACK];
static struct rt_messagequeue s_cmd_mq;
static rt_uint8_t s_cmd_pool[8 * sizeof(session_cmd_t)];
static struct rt_timer s_timer;
static sess_kind_t s_kind;
static rt_tick_t s_alarm_start;
static uint8_t s_inited;

static void session_timer_cb(void *param)
{
    session_cmd_t cmd;

    (void)param;
    cmd.cmd = SESSION_CMD_TICK;
    (void)rt_mq_send(&s_cmd_mq, &cmd, sizeof(cmd));
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

static void arm_next_timer(void)
{
    rt_tick_t elapsed_ms;

    if (s_kind == SESS_KIND_ON)
    {
        arm_timer_ms(SESSION_MS_10MIN);
        return;
    }

    if (s_kind != SESS_KIND_ALARM)
    {
        return;
    }

    elapsed_ms = (rt_tick_get() - s_alarm_start) * 1000u / RT_TICK_PER_SECOND;
    if (elapsed_ms >= SESSION_MS_48H)
    {
        rt_kprintf("[SESSION] 48h reached → ON\n");
        s_kind = SESS_KIND_NONE;
        mode_post_event(MODE_EVT_ALARM_EXPIRE);
        return;
    }
    if (elapsed_ms < SESSION_MS_24H)
    {
        arm_timer_ms(SESSION_MS_2MIN);
    }
    else
    {
        arm_timer_ms(SESSION_MS_5MIN);
    }
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

    if (!sim_present())
    {
        rt_kprintf("[SESSION] no SIM card, skip GNSS/RDSS\n");
        return;
    }

#if !USE_GNSS
    rt_kprintf("[SESSION] GNSS not built, skip cycle\n");
    return;
#else
    if (gnss_start_fix() != RT_EOK)
    {
        rt_kprintf("[SESSION] gnss_start_fix fail\n");
        return;
    }

    if (rt_mq_recv(gnss_result_mq(), &gmsg, sizeof(gmsg),
                   rt_tick_from_millisecond(GNSS_FIX_TIMEOUT_MS + 5000)) != RT_EOK)
    {
        rt_kprintf("[SESSION] gnss result timeout\n");
        return;
    }
    if (!gmsg.ok || !gmsg.fix.valid)
    {
        rt_kprintf("[SESSION] gnss fail reason=%u, skip RDSS\n", gmsg.reason);
        return;
    }

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
    if (rt_mq_recv(rdss_result_mq(), &rmsg, sizeof(rmsg),
                   rt_tick_from_millisecond(RDSS_BEAM_TIMEOUT_MS + RDSS_FKI_TIMEOUT_MS + 5000)) != RT_EOK)
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
            rt_timer_stop(&s_timer);
            radio_abort();
            s_kind = SESS_KIND_ALARM;
            s_alarm_start = rt_tick_get();
            rt_kprintf("[SESSION] alarm start\n");
            run_one_cycle(1);
            if (s_kind == SESS_KIND_ALARM)
            {
                arm_next_timer();
            }
            break;

        case SESSION_CMD_START_ON:
            rt_timer_stop(&s_timer);
            radio_abort();
            s_kind = SESS_KIND_ON;
            rt_kprintf("[SESSION] on start (10min)\n");
            run_one_cycle(0);
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
                run_one_cycle(1);
                if (s_kind == SESS_KIND_ALARM)
                {
                    arm_next_timer();
                }
            }
            else if (s_kind == SESS_KIND_ON)
            {
                run_one_cycle(0);
                if (s_kind == SESS_KIND_ON)
                {
                    arm_next_timer();
                }
            }
            break;

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

static void session_post(uint8_t c)
{
    session_cmd_t cmd;

    if (session_ensure_init() != 0)
    {
        return;
    }
    cmd.cmd = c;
    if (rt_mq_send(&s_cmd_mq, &cmd, sizeof(cmd)) != RT_EOK)
    {
        rt_kprintf("[SESSION] mq full cmd=%u\n", (unsigned)c);
    }
}

void session_alarm_start(void)
{
    session_post(SESSION_CMD_START_ALARM);
}

void session_alarm_stop(void)
{
    if (!s_inited)
    {
        return;
    }
    session_post(SESSION_CMD_STOP);
}

void session_on_start(void)
{
    session_post(SESSION_CMD_START_ON);
}

void session_on_stop(void)
{
    if (!s_inited)
    {
        return;
    }
    session_post(SESSION_CMD_STOP);
}

void session_lowbatt_once(void)
{
    session_post(SESSION_CMD_START_LB);
}

void session_stop_all(void)
{
    if (!s_inited)
    {
        return;
    }
    session_post(SESSION_CMD_STOP);
}

void session_test_once(void)
{
    session_post(SESSION_CMD_START_TEST);
}
