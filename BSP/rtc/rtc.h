/**
 * @file rtc.h
 * @brief 硬件 RTC（LSE 32768Hz）；校时走消息队列。产品由 session 投 GNSS Unix，不用 RDSS。
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

/** 读当前 Unix 时间（UTC 秒）；未校时或失败返回 0（默认 2020 历不当墙钟） */
uint32_t rtc_get_unix(void);

/**
 * 非阻塞投递校时（GNSS/RDSS/CLI 用）。
 * 由 RTC 校时线程落盘，调用方不直接碰硬件。
 */
rt_err_t rtc_post_unix(uint32_t unix_sec, uint8_t src);

/** 1=曾被外部校准过（本上电或 BKP 标记） */
int rtc_is_synced(void);

/** 10s 心跳回调槽：RTC 不 include LED/session/mode */
#define RTC_WU_SLOT_LED     0u
#define RTC_WU_SLOT_SESS    1u
#define RTC_WU_SLOT_MODE    2u
#define RTC_WU_SLOT_N       3u

typedef void (*rtc_wu_hook_t)(void);

void rtc_wu_hook_set(uint8_t slot, rtc_wu_hook_t fn);
/** period_s=10 → 每 10 秒 EXTI20 + RTC_WKUP。ISR 只清标志、喂狗、调钩子 */
int rtc_wu_start(uint32_t period_s);
void rtc_wu_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_RTC_H__ */
