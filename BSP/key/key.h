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
/** 任务轮询周期（无中断时也醒来；四路统一采样节拍） */
#ifndef KEY_POLL_MS
#define KEY_POLL_MS             10
#endif
/** USB 插入：积分加到此值确认（≈160ms）；无效只减 1，不清零 */
#ifndef USB_IN_CONFIRM_CNT
#define USB_IN_CONFIRM_CNT      8        /* ≈80ms */
#endif
/** USB 拔出：积分加到此值确认（≈240ms）；又插上只减 1，不清零 */
#ifndef USB_OUT_CONFIRM_CNT
#define USB_OUT_CONFIRM_CNT     24
#endif
#ifndef SIM_IN_CONFIRM_CNT
#define SIM_IN_CONFIRM_CNT      16       /* ≈160ms */
#endif
#ifndef SIM_OUT_CONFIRM_CNT
#define SIM_OUT_CONFIRM_CNT     24       /* ≈240ms */
#endif
#ifndef FALL_ON_CONFIRM_CNT
#define FALL_ON_CONFIRM_CNT     16       /* ≈160ms 认定 */
#endif
#ifndef FALL_OFF_CONFIRM_CNT
#define FALL_OFF_CONFIRM_CNT    24       /* ≈240ms 撤销 */
#endif
#ifndef SOS_SHORT_CNT
#define SOS_SHORT_CNT           24       /* ≈240ms */
#endif
#ifndef SOS_LONG_CNT
#define SOS_LONG_CNT            (SOS_SHORT_CNT * 10u)  /* ≈2.4s */
#endif
/** SOS 确认松开（连续无效）；按下仍用积分加减 */
#ifndef SOS_REL_CNT
#define SOS_REL_CNT             8        /* ≈80ms */
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
 * USB 已确认在位：1=插入积分已满且尚未确认拔出。
 * CHARGE / STOP2 / 采电跟这个走，不要每圈直接读 PA7。
 */
int usb_present(void);

/**
 * 有通道正在滤波（或 SOS 还按着）：1=忙。
 * MODE 真关机前必须查：STOP2 只认沿，忙着时睡下去这次按下就丢了。
 */
int key_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* __KEY_H__ */
