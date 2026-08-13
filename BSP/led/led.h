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

/** ON：周期 / 亮时长（三灯同闪） */
#ifndef LED_ON_PERIOD_MS
#define LED_ON_PERIOD_MS        10000
#endif
#ifndef LED_ON_PULSE_MS
#define LED_ON_PULSE_MS         200
#endif

/** ALARM：周期 / 亮时长（三灯同闪） */
#ifndef LED_ALARM_PERIOD_MS
#define LED_ALARM_PERIOD_MS     5000
#endif
#ifndef LED_ALARM_PULSE_MS
#define LED_ALARM_PULSE_MS      100
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
#define LED_CHARGE_FULL_PCT     98
#endif

/** 消息 kind */
#define LED_MSG_PERCENT         1u
#define LED_MSG_MODE            2u

typedef struct
{
    uint8_t kind;   /* LED_MSG_PERCENT / LED_MSG_MODE */
    uint8_t value;  /* percent 0~100 或 mode_state_t */
} led_msg_t;

int led_init(void);

/** ADC / MODE 推送（非阻塞；队列满则丢弃最新前一条） */
void led_post_percent(uint8_t percent);
void led_post_mode(uint8_t mode);

#ifdef __cplusplus
}
#endif

#endif /* __LED_H__ */
