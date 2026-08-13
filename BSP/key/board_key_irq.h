/**
 * @file board_key_irq.h
 * @brief SOS_KEY / USB_IN / FALL_KEY / SIM 外部中断初始化
 *
 * board 只负责挂 EXTI；业务由 BSP/key 通过 hook 感知。
 */
#ifndef __BOARD_KEY_IRQ_H__
#define __BOARD_KEY_IRQ_H__

#include <rtdevice.h>
#include "board_pins.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    BOARD_KEY_SOS = 0,
    BOARD_KEY_USB_IN,
    BOARD_KEY_FALL,
    BOARD_KEY_SIM,
} board_key_id_t;

/** ISR 内调用：务必短小（置位 + 释放信号量），勿打印/阻塞 */
typedef void (*board_key_isr_hook_t)(board_key_id_t id, void *user);

/* SOS：下降沿；USB/SIM：双沿；FALL：上升沿 */
#ifndef BOARD_SOS_KEY_IRQ_MODE
#define BOARD_SOS_KEY_IRQ_MODE   PIN_IRQ_MODE_FALLING
#endif
#ifndef BOARD_USB_IN_IRQ_MODE
#define BOARD_USB_IN_IRQ_MODE    PIN_IRQ_MODE_RISING_FALLING
#endif
#ifndef BOARD_FALL_KEY_IRQ_MODE
#define BOARD_FALL_KEY_IRQ_MODE  PIN_IRQ_MODE_RISING
#endif
#ifndef BOARD_SIM_IRQ_MODE
#define BOARD_SIM_IRQ_MODE       PIN_IRQ_MODE_RISING_FALLING
#endif

/**
 * 配置 GPIO 输入中断。
 * @param hook 可为 RT_NULL（仅初始化脚，不回调）
 * @return 成功 0
 */
int board_key_irq_init(board_key_isr_hook_t hook, void *user);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_KEY_IRQ_H__ */
