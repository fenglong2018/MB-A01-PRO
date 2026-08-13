/**
 * @file rtc_hw.c
 * @brief N32WB452 日历 RTC：LSE 32768 → 1Hz；校时线程消费 mq
 *
 * GNSS/RDSS → rtc_post_unix() → mq → rtc 线程 → 写硬件
 * 无业务方直接调用写接口。
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "rtc.h"
#include "n32wb452.h"
#include "n32wb452_rcc.h"
#include "n32wb452_pwr.h"
#include "n32wb452_bkp.h"
#include "n32wb452_rtc.h"

#if USE_RTC

#define RTC_BKP_MAGIC           0xA55Au
#define RTC_BKP_SYNC_FLAG       0x5A5Au

typedef struct
{
    uint32_t unix_sec;
    uint8_t  src;
} rtc_sync_msg_t;

static struct rt_device s_rtc_dev;
static uint8_t s_synced;
static uint8_t s_hw_ok;

static struct rt_thread s_thread;
static rt_uint8_t s_stack[RTC_THREAD_STACK];
static struct rt_messagequeue s_sync_mq;
static rt_uint8_t s_sync_pool[RTC_SYNC_MQ_DEPTH * sizeof(rtc_sync_msg_t)];

static uint8_t tm_wday_to_rtc(int tm_wday)
{
    if (tm_wday == 0)
    {
        return RTC_WEEKDAY_SUNDAY;
    }
    return (uint8_t)tm_wday;
}

static int rtc_wday_to_tm(uint8_t rtc_wday)
{
    if (rtc_wday == RTC_WEEKDAY_SUNDAY)
    {
        return 0;
    }
    return (int)rtc_wday;
}

static int lse_enable_wait(uint32_t timeout_ms)
{
    uint32_t loops;

    loops = timeout_ms * (SystemCoreClock / 8000u);
    if (loops < 1000u)
    {
        loops = 1000u;
    }

    RCC_ConfigLse(RCC_LSE_ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_LSERD) == RESET)
    {
        if (loops-- == 0)
        {
            return -1;
        }
    }
    return 0;
}

static int rtc_calendar_config(void)
{
    RTC_InitType init;
    RTC_TimeType t;
    RTC_DateType d;

    RCC_ConfigRtcClk(RCC_RTCCLK_SRC_LSE);
    RCC_EnableRtcClk(ENABLE);
    if (RTC_WaitForSynchro() == ERROR)
    {
        return -1;
    }

    RTC_EnableWriteProtection(DISABLE);

    init.RTC_HourFormat   = RTC_24HOUR_FORMAT;
    init.RTC_AsynchPrediv = 127;
    init.RTC_SynchPrediv  = 255;
    if (RTC_Init(&init) == ERROR)
    {
        RTC_EnableWriteProtection(ENABLE);
        return -1;
    }

    d.Year    = 20;
    d.Month   = RTC_MONTH_JANUARY;
    d.Date    = 1;
    d.WeekDay = RTC_WEEKDAY_WEDNESDAY;
    t.Hours   = 0;
    t.Minutes = 0;
    t.Seconds = 0;
    t.H12     = RTC_AM_H12;

    if (RTC_SetDate(RTC_FORMAT_BIN, &d) == ERROR)
    {
        RTC_EnableWriteProtection(ENABLE);
        return -1;
    }
    if (RTC_ConfigTime(RTC_FORMAT_BIN, &t) == ERROR)
    {
        RTC_EnableWriteProtection(ENABLE);
        return -1;
    }

    RTC_EnableWriteProtection(ENABLE);
    BKP_WriteBkpData(BKP_DAT1, RTC_BKP_MAGIC);
    return 0;
}

static rt_err_t rtc_get_time_t(time_t *out)
{
    RTC_TimeType t;
    RTC_DateType d;
    struct tm tm;

    if (out == RT_NULL)
    {
        return -RT_EINVAL;
    }
    if (!s_hw_ok)
    {
        return -RT_ERROR;
    }

    RTC_GetTime(RTC_FORMAT_BIN, &t);
    RTC_GetDate(RTC_FORMAT_BIN, &d);

    memset(&tm, 0, sizeof(tm));
    tm.tm_year = (int)d.Year + 100;
    tm.tm_mon  = (int)d.Month - 1;
    tm.tm_mday = (int)d.Date;
    tm.tm_hour = (int)t.Hours;
    tm.tm_min  = (int)t.Minutes;
    tm.tm_sec  = (int)t.Seconds;
    tm.tm_wday = rtc_wday_to_tm(d.WeekDay);
    tm.tm_isdst = 0;

    *out = mktime(&tm);
    return RT_EOK;
}

/** 仅 RTC 校时线程 / 设备层调用 */
static rt_err_t rtc_apply_unix(uint32_t unix_sec, uint8_t src)
{
    struct tm *p;
    struct tm tm_copy;
    RTC_TimeType t;
    RTC_DateType d;
    time_t now = (time_t)unix_sec;

    if (!s_hw_ok || (unix_sec == 0))
    {
        return -RT_ERROR;
    }

    rt_enter_critical();
    p = localtime(&now);
    if (p == RT_NULL)
    {
        rt_exit_critical();
        return -RT_ERROR;
    }
    memcpy(&tm_copy, p, sizeof(tm_copy));
    rt_exit_critical();

    if (tm_copy.tm_year < 100)
    {
        return -RT_EINVAL;
    }

    d.Year    = (uint8_t)(tm_copy.tm_year - 100);
    d.Month   = (uint8_t)(tm_copy.tm_mon + 1);
    d.Date    = (uint8_t)tm_copy.tm_mday;
    d.WeekDay = tm_wday_to_rtc(tm_copy.tm_wday);
    t.Hours   = (uint8_t)tm_copy.tm_hour;
    t.Minutes = (uint8_t)tm_copy.tm_min;
    t.Seconds = (uint8_t)tm_copy.tm_sec;
    t.H12     = RTC_AM_H12;

    RTC_EnableWriteProtection(DISABLE);
    if (RTC_SetDate(RTC_FORMAT_BIN, &d) == ERROR)
    {
        RTC_EnableWriteProtection(ENABLE);
        return -RT_ERROR;
    }
    if (RTC_ConfigTime(RTC_FORMAT_BIN, &t) == ERROR)
    {
        RTC_EnableWriteProtection(ENABLE);
        return -RT_ERROR;
    }
    RTC_EnableWriteProtection(ENABLE);

    s_synced = 1;
    BKP_WriteBkpData(BKP_DAT2, RTC_BKP_SYNC_FLAG);
    rt_kprintf("[RTC] synced unix=%lu src=%u\n",
               (unsigned long)unix_sec, (unsigned)src);
    return RT_EOK;
}

static void rtc_sync_thread_entry(void *param)
{
    rtc_sync_msg_t msg;

    (void)param;
    while (1)
    {
        if (rt_mq_recv(&s_sync_mq, &msg, sizeof(msg), RT_WAITING_FOREVER) != RT_EOK)
        {
            continue;
        }
        (void)rtc_apply_unix(msg.unix_sec, msg.src);
    }
}

static rt_err_t rtc_dev_control(rt_device_t dev, int cmd, void *args)
{
    (void)dev;

    switch (cmd)
    {
    case RT_DEVICE_CTRL_RTC_GET_TIME:
        return rtc_get_time_t((time_t *)args);
    case RT_DEVICE_CTRL_RTC_SET_TIME:
        /* 设备写也走队列，统一落盘路径 */
        return rtc_post_unix((uint32_t)(*(time_t *)args), RTC_SRC_CLI);
    default:
        return -RT_ERROR;
    }
}

#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops s_rtc_ops =
{
    RT_NULL,
    RT_NULL,
    RT_NULL,
    RT_NULL,
    RT_NULL,
    rtc_dev_control,
};
#endif

uint32_t rtc_get_unix(void)
{
    time_t t = 0;

    if (rtc_get_time_t(&t) != RT_EOK)
    {
        return 0;
    }
    return (uint32_t)t;
}

rt_err_t rtc_post_unix(uint32_t unix_sec, uint8_t src)
{
    rtc_sync_msg_t msg;

    if ((unix_sec == 0) || !s_hw_ok)
    {
        return -RT_EINVAL;
    }

    msg.unix_sec = unix_sec;
    msg.src = src;
    if (rt_mq_send(&s_sync_mq, &msg, sizeof(msg)) != RT_EOK)
    {
        /* 队列满：丢最旧再投一次 */
        rtc_sync_msg_t dump;
        (void)rt_mq_recv(&s_sync_mq, &dump, sizeof(dump), 0);
        if (rt_mq_send(&s_sync_mq, &msg, sizeof(msg)) != RT_EOK)
        {
            return -RT_EFULL;
        }
    }
    return RT_EOK;
}

int rtc_is_synced(void)
{
    if (s_synced)
    {
        return 1;
    }
    if (s_hw_ok && (BKP_ReadBkpData(BKP_DAT2) == RTC_BKP_SYNC_FLAG))
    {
        return 1;
    }
    return 0;
}

static int rtc_hw_setup(void)
{
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_PWR | RCC_APB1_PERIPH_BKP, ENABLE);
    PWR_BackupAccessEnable(ENABLE);

    if (lse_enable_wait(RTC_LSE_TIMEOUT_MS) != 0)
    {
        rt_kprintf("[RTC] LSE timeout\n");
        return -1;
    }

    if (BKP_ReadBkpData(BKP_DAT1) != RTC_BKP_MAGIC)
    {
        RCC_EnableBackupReset(ENABLE);
        RCC_EnableBackupReset(DISABLE);
        PWR_BackupAccessEnable(ENABLE);
        if (lse_enable_wait(RTC_LSE_TIMEOUT_MS) != 0)
        {
            rt_kprintf("[RTC] LSE timeout after BK reset\n");
            return -1;
        }
        if (rtc_calendar_config() != 0)
        {
            rt_kprintf("[RTC] calendar config fail\n");
            return -1;
        }
        rt_kprintf("[RTC] first init LSE ok\n");
    }
    else
    {
        RCC_EnableRtcClk(ENABLE);
        if (RTC_WaitForSynchro() == ERROR)
        {
            rt_kprintf("[RTC] synchro fail\n");
            return -1;
        }
        rt_kprintf("[RTC] resume ok\n");
    }

    s_synced = (BKP_ReadBkpData(BKP_DAT2) == RTC_BKP_SYNC_FLAG) ? 1u : 0u;
    s_hw_ok = 1;
    return 0;
}

int rtc_hw_init(void)
{
    rt_err_t err;

    s_hw_ok = 0;
    s_synced = 0;

    if (rtc_hw_setup() != 0)
    {
        return -1;
    }

    s_rtc_dev.type = RT_Device_Class_RTC;
#ifdef RT_USING_DEVICE_OPS
    s_rtc_dev.ops = &s_rtc_ops;
#else
    s_rtc_dev.init = RT_NULL;
    s_rtc_dev.open = RT_NULL;
    s_rtc_dev.close = RT_NULL;
    s_rtc_dev.read = RT_NULL;
    s_rtc_dev.write = RT_NULL;
    s_rtc_dev.control = rtc_dev_control;
#endif
    if (rt_device_register(&s_rtc_dev, "rtc", RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("[RTC] register fail\n");
        return -1;
    }

    rt_mq_init(&s_sync_mq, "rtcsync", s_sync_pool, sizeof(rtc_sync_msg_t),
               sizeof(s_sync_pool), RT_IPC_FLAG_FIFO);

    err = rt_thread_init(&s_thread, "rtc", rtc_sync_thread_entry, RT_NULL,
                         s_stack, sizeof(s_stack), RTC_THREAD_PRIO, 10);
    if (err != RT_EOK)
    {
        rt_kprintf("[RTC] thread init fail\n");
        return -1;
    }
    rt_thread_startup(&s_thread);
    rt_kprintf("[RTC] ready synced=%d (mq+thread)\n", rtc_is_synced());
    return 0;
}

static int app_rtc_init(void)
{
    return rtc_hw_init();
}
/* 需在调度器起来后跑线程：用 APP 级 init（与 key/rdss 同档） */
INIT_APP_EXPORT(app_rtc_init);

#else /* !USE_RTC */

int rtc_hw_init(void) { return 0; }
uint32_t rtc_get_unix(void) { return 0; }
rt_err_t rtc_post_unix(uint32_t s, uint8_t src) { (void)s; (void)src; return -RT_ERROR; }
int rtc_is_synced(void) { return 0; }

#endif /* USE_RTC */
