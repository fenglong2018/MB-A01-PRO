/**
 * @file mode.h
 * @brief 整机模式状态机（见 app/mode/fsm.md）
 */
#ifndef __MODE_H__
#define __MODE_H__

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MODE_THREAD_STACK
#define MODE_THREAD_STACK       1024
#endif
#ifndef MODE_THREAD_PRIO
#define MODE_THREAD_PRIO        14
#endif
#ifndef MODE_BATT_MS
#define MODE_BATT_MS            5000
#endif

typedef enum
{
    MODE_ST_OFF = 0,
    MODE_ST_BATT,
    MODE_ST_ON,
    MODE_ST_ALARM,
    MODE_ST_CHARGE,
    MODE_ST_FORCE_OFF,     /**< 低压强制关机；仅 USB→CHARGE 解除 */
    MODE_ST_PASSTHRU,      /**< 调试透传；通道见 MODE_PT_* */
    MODE_ST_LOW_BATT,      /**< 低电预警：只发 1 条短报文后回 ON */
    /* MODE_ST_SLEEP：假关机预留，暂不实施 */
} mode_state_t;

/** 透传通道掩码（可组合 = GNSS+RDSS） */
#define MODE_PT_GNSS            (1u << 0)
#define MODE_PT_RDSS            (1u << 1)

/** KEY / ADC / 定时 / 透传 / 会话 → MODE 事件位（rt_event） */
#define MODE_EVT_SOS_SHORT      (1u << 0)
#define MODE_EVT_SOS_LONG       (1u << 1)
#define MODE_EVT_FALL           (1u << 2)
#define MODE_EVT_USB_IN         (1u << 3)
#define MODE_EVT_USB_OUT        (1u << 4)
#define MODE_EVT_BATT_TO        (1u << 5)
#define MODE_EVT_BAT_PROTECT    (1u << 6)  /* ADC：进入保护阈值 */
#define MODE_EVT_PT_APPLY       (1u << 7)  /* 应用 s_pt_req_flags */
#define MODE_EVT_ALARM_EXPIRE   (1u << 8)  /* 告警会话满 48h → ON */
#define MODE_EVT_BAT_WARN       (1u << 9)  /* ADC：进入预警 → LOW_BATT */
#define MODE_EVT_LOW_BATT_DONE  (1u << 10) /* LOW_BATT 单次报文结束 → ON */

/** 建事件+线程；上电采样 USB/保护。一般由 INIT_APP_EXPORT 调用 */
int mode_init(void);

/** 读当前模式（OFF/ON/ALARM/…） */
mode_state_t mode_state_get(void);

/** 当前透传通道掩码；非 PASSTHRU 时为 0 */
uint8_t mode_passthru_flags_get(void);

/**
 * CLI/stream：设置透传通道（0=退出透传）。
 * 仅 ON/CHARGE（或已在 PASSTHRU）可开；保护/OFF/ALARM 等拒绝。
 * 异步投递 MODE_EVT_PT_APPLY，由 mode 线程执行。
 */
int mode_passthru_set(uint8_t flags);

/**
 * 上报事件到位图（可组合）。调用方：KEY / ADC / session。
 * 禁止在 ISR 里做重活；ISR 应先唤醒业务线程再 post（KEY 已如此）。
 */
void mode_post_event(rt_uint32_t evt);

#ifdef __cplusplus
}
#endif

#endif /* __MODE_H__ */
