/**
 * @file key.h
 * @brief 按键功能模块对外接口
 */
#ifndef __KEY_H__
#define __KEY_H__

#include "board_key_irq.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef KEY_THREAD_STACK
#define KEY_THREAD_STACK        1024
#endif
#ifndef KEY_THREAD_PRIO
#define KEY_THREAD_PRIO         15
#endif
/** 任务轮询周期（无中断时也醒来；SOS 采样节拍） */
#ifndef KEY_POLL_MS
#define KEY_POLL_MS             10
#endif
/** 简单滤波：连续有效 KEY_FILTER_CNT 次后确认，再等无效电平后 rearm */
#ifndef KEY_FILTER_CNT
#define KEY_FILTER_CNT          8
#endif
/**
 * SOS：连续有效计数（单位 KEY_POLL_MS）。
 * - 达 SOS_LONG_CNT → 仅长按
 * - 未达长按前松手且 hit∈[SOS_SHORT_CNT, SOS_LONG_CNT) → 短按
 * - hit < SOS_SHORT_CNT 即松手 → 抖动，清零并恢复中断
 *
 * 长按要按人手时长给（默认 2s）；给成一两百毫秒的话正常一按就是长按，
 * 短按窗口人手按不出来。
 */
#ifndef SOS_SHORT_CNT
#define SOS_SHORT_CNT           5       /* ≈50ms 消抖 */
#endif
#ifndef SOS_LONG_CNT
#define SOS_LONG_CNT            200     /* ≈2s */
#endif

/** pending 位图：与 board_key_id_t 对应 */
#define KEY_EVT_SOS             (1u << BOARD_KEY_SOS)
#define KEY_EVT_USB_IN          (1u << BOARD_KEY_USB_IN)
#define KEY_EVT_FALL            (1u << BOARD_KEY_FALL)
#define KEY_EVT_SIM             (1u << BOARD_KEY_SIM)

/**
 * 初始化 EXTI + key 线程；成功返回 0。
 * USE_KEY 时由 INIT_APP_EXPORT 自动调用。
 */
int key_init(void);

/**
 * 北斗卡在位：1=有卡（RD_BD_SIMCARD 低），0=无卡。
 * 非 MODE 状态；仅能力位，供 session/GNSS/RDSS 门控。
 */
int sim_present(void);

/**
 * 有通道正在滤波（或 SOS 还按着）：1=忙。
 * MODE 真关机前必须查：STOP2 只认沿，忙着时睡下去这次按下就丢了。
 */
int key_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* __KEY_H__ */
