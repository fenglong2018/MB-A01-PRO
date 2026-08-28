/**
 * @file led.h
 * @brief 电量 LED 指示（消息驱动；规则见 README）
 *
 * 硬件：LED1/2/3 低电平点亮；驱动内逻辑仍按 1=亮/0=灭，写 GPIO 时取反。
 */
#ifndef __LED_H__
#define __LED_H__

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LED_THREAD_STACK
#define LED_THREAD_STACK        1024
#endif
#ifndef LED_THREAD_PRIO
#define LED_THREAD_PRIO         21
#endif

/** 流水单步时长 */
#ifndef LED_FLOW_STEP_MS
#define LED_FLOW_STEP_MS        300
#endif
/** 充电流水轮间间隔 */
#ifndef LED_FLOW_GAP_MS
#define LED_FLOW_GAP_MS         300
#endif

/** ON：周期 10s / 亮 100ms */
#ifndef LED_ON_PERIOD_MS
#define LED_ON_PERIOD_MS        10000
#endif
#ifndef LED_ON_PULSE_MS
#define LED_ON_PULSE_MS         100
#endif
/** 无卡 ON 提示：每秒亮 100ms（MODE 窗口 5s） */
#ifndef LED_ON_NOSIM_PERIOD_MS
#define LED_ON_NOSIM_PERIOD_MS  1000
#endif

/** ALARM：周期 10s；双闪 100ms + 灭 200ms + 100ms */
#ifndef LED_ALARM_PERIOD_MS
#define LED_ALARM_PERIOD_MS     10000
#endif
#ifndef LED_ALARM_PULSE_MS
#define LED_ALARM_PULSE_MS      100
#endif
#ifndef LED_ALARM_GAP_MS
#define LED_ALARM_GAP_MS        200
#endif

/** 流水档位（与保护/预警百分比无关） */
#ifndef LED_BAND_LOW_PCT
#define LED_BAND_LOW_PCT        30
#endif
#ifndef LED_BAND_HIGH_PCT
#define LED_BAND_HIGH_PCT       70
#endif

/** 充电满电常亮阈值 */
#ifndef LED_CHARGE_FULL_PCT
#define LED_CHARGE_FULL_PCT     96
#endif

/** 消息 kind */
#define LED_MSG_PERCENT         1u
#define LED_MSG_MODE            2u
#define LED_MSG_HB              3u  /* RTC 10s：打一拍灯，ISR 可投 */

typedef struct
{
    uint8_t kind;   /* LED_MSG_PERCENT / LED_MSG_MODE */
    uint8_t value;  /* percent 0~100 或 mode_state_t */
} led_msg_t;

int led_init(void);

/** ADC / MODE 推送（非阻塞；队列满则丢弃最新前一条） */
void led_post_percent(uint8_t percent);
void led_post_mode(uint8_t mode);

/** ON 心跳周期；0=恢复 10s。无卡提示用 1000 */
void led_set_on_period_ms(uint32_t period_ms);

/** 同步灭灯并停动画（STOP2 / ON 假关机前用；随后到来的 ON 消息会被丢掉） */
void led_output_off(void);

/** 允许再显示 ON/ALARM（下一拍发信 SHOT_BUSY） */
void led_dark_unlock(void);

/** 后续脉冲等 RTC HB。0=关。不立刻灭灯（进 ON 先闪 100ms 再用） */
void led_use_rtc_hb(uint8_t enable);

/** 立刻灭灯并等 RTC（进 STOP0 假关机，避免 100ms 脉冲冻在常亮） */
void led_wait_rtc_hb(uint8_t enable);
/** RTC ISR 钩子：只投 LED_MSG_HB */
void led_on_rtc_wu(void);

#ifdef __cplusplus
}
#endif

#endif /* __LED_H__ */
