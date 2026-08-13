/**
 * @file rtc.h
 * @brief 硬件 RTC（LSE 32768Hz）；校时走消息队列，与 GNSS/RDSS 解耦
 */
#ifndef __BSP_RTC_H__
#define __BSP_RTC_H__

#include <stdint.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RTC_LSE_TIMEOUT_MS
#define RTC_LSE_TIMEOUT_MS      2000
#endif
#ifndef RTC_THREAD_STACK
#define RTC_THREAD_STACK        1024
#endif
#ifndef RTC_THREAD_PRIO
#define RTC_THREAD_PRIO         20
#endif
#ifndef RTC_SYNC_MQ_DEPTH
#define RTC_SYNC_MQ_DEPTH       4
#endif

/** 校时来源（消息字段，仅日志/策略用） */
#define RTC_SRC_UNKNOWN         0u
#define RTC_SRC_GNSS            1u
#define RTC_SRC_RDSS            2u
#define RTC_SRC_CLI             3u

/** 初始化 LSE + 日历 + 注册 "rtc" + 启动校时线程。成功 0。 */
int rtc_hw_init(void);

/** 读当前 Unix 时间（UTC 秒）；失败返回 0 */
uint32_t rtc_get_unix(void);

/**
 * 非阻塞投递校时（GNSS/RDSS/CLI 用）。
 * 由 RTC 校时线程落盘，调用方不直接碰硬件。
 */
rt_err_t rtc_post_unix(uint32_t unix_sec, uint8_t src);

/** 1=曾被外部校准过（本上电或 BKP 标记） */
int rtc_is_synced(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_RTC_H__ */
