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
/** 无卡 ON 一拍结束后先亮灯提示再真关机；0=立刻真关机。不进 JSON */
#ifndef MODE_ON_NOSIM_HINT_MS
#define MODE_ON_NOSIM_HINT_MS   5000
#endif
/**
 * 上电/STOP2 醒来后多久之内不真关机。
 * 留给各 INIT_APP + KEY 线程把复位时的按键电平跑成短按/长按。
 */
#ifndef MODE_BOOT_HOLD_MS
#define MODE_BOOT_HOLD_MS       400
#endif
/** CHARGE/ON 等已开狗时，mode 线程最多等这么久就回来喂一次（须 < IWDG ≈26s） */
#ifndef MODE_IWDG_FEED_MS
#define MODE_IWDG_FEED_MS       5000
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
    MODE_ST_LOW_BATT,      /**< 低电预警 overlay：1 条 N 后回进入前的态 */
    MODE_ST_FAKE_OFF,      /**< 假关机：发完一拍；BKP.resume 为 ON 或 ALARM */
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
#define MODE_EVT_SIM            (1u << 8)  /* SIM 插拔确认；假关机 ON 用来开关 10s 灯 */
#define MODE_EVT_BAT_WARN       (1u << 9)  /* ADC：进入预警 → LOW_BATT */
#define MODE_EVT_LOW_BATT_DONE  (1u << 10) /* LOW_BATT 单次报文结束 → 进入前的态 */
#define MODE_EVT_SHOT_BUSY      (1u << 11) /* session 开跑一拍：FAKE_OFF→resume */
#define MODE_EVT_SHOT_IDLE      (1u << 12) /* session 一拍结束：可进 FAKE_OFF */
#define MODE_EVT_RTC_WU         (1u << 13) /* RTC 10s：FAKE_OFF 空载采 1 次 */
#define MODE_EVT_BAT_OK         (1u << 14)  /* ADC：回到 OK，清 LOW_BATT 已发标志 */

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

/**
 * ALARM 第一次 GNSS 校时后锁节奏锚点（2/5/10min；session 调用）。
 * 逻辑告警（ALARM 或 FAKE_OFF←ALARM）且 unix≠0 才写 BKP。
 */
void mode_alarm_anchor_latch(uint32_t unix_sec);

#ifdef __cplusplus
}
#endif

#endif /* __MODE_H__ */
