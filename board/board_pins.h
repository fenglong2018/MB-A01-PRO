/**
 * @file board_pins.h
 * @brief 整板 IO / 外设引脚定义（放 config，与产品板型配置同层）
 *
 * 数据来源：MCU 引脚功能表（评估板原理图网络名）
 * 用法：#include "board_pins.h"（config 已在统一 -I 路径中）
 *
 * 注意：
 * - USART2 在 PB4/PB5 需 GPIO_RMP3_USART2，且 PB4 默认 NJTRST，
 *   使用前需 SWJ 重映射（如 GPIO_RMP_SW_JTAG_SW_ENABLE）释放 PB3/PB4/PA15。
 * - LED3 软件控制脚为 PB2；表中 BOOT0 网络名亦为 LED3，属启动配置脚，勿当 GPIO。
 * - USB_D+/D- 由 USB 外设占用，一般不再作普通 GPIO 初始化。
 */
#ifndef __BOARD_PINS_H__
#define __BOARD_PINS_H__

#include "n32wb452.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* 按键 / 唤醒                                                                */
/* -------------------------------------------------------------------------- */
/* Pin10 PA0-WKUP  KEY1  输入  唤醒/按键 */
#define SOS_KEY_PORT                 GPIOA
#define SOS_KEY_PIN                  GPIO_PIN_0
#define SOS_KEY_CLK                  RCC_APB2_PERIPH_GPIOA
#define SOS_KEY_PIN_SOURCE           GPIO_PIN_SOURCE0
#define SOS_KEY_PORT_SOURCE          GPIOA_PORT_SOURCE
/* RT-Thread pin 编号：PA0=0（供 rt_pin_xxx 使用） */
#define SOS_KEY_RT_PIN               0

/* -------------------------------------------------------------------------- */
/* ADC                                                                        */
/* -------------------------------------------------------------------------- */
/* Pin11 PA4  AD_BAT  模拟输入  电池电压采样
 * 分压：BAT -- R16(1.2M) -- AD_BAT -- R17(3.3M) -- GND
 * Vbat = Vpin * (R16+R17)/R17 = Vpin * 15/11
 * 内部基准：ADC_CH_INT_VREF (CH18, 1.2V) 用于算 VDDA
 */
#define BAT_ADC_BAT_PORT               GPIOA
#define BAT_ADC_BAT_PIN                GPIO_PIN_4
#define BAT_ADC_BAT_CLK                RCC_APB2_PERIPH_GPIOA
#define BAT_ADC_BAT_ADC_CH             ADC_CH_4
#define BAT_DIV_R16_OHM                1200000u
#define BAT_DIV_R17_OHM                3300000u

/* -------------------------------------------------------------------------- */
/* 检测 / 控制 GPIO                                                           */
/* -------------------------------------------------------------------------- */
/* Pin12 PA5  RD_SIMCARD  SIM 卡检测（低=有卡） */
#define RD_BD_SIMCARD_PORT           GPIOA
#define RD_BD_SIMCARD_PIN            GPIO_PIN_5
#define RD_BD_SIMCARD_CLK            RCC_APB2_PERIPH_GPIOA
#define RD_BD_SIMCARD_RT_PIN         5   /* PA5 */

/* Pin14 PA7  MCU_I_USB_IN  USB 插入检测 */
#define USB_IN_PORT               GPIOA
#define USB_IN_PIN                GPIO_PIN_7
#define USB_IN_CLK                RCC_APB2_PERIPH_GPIOA
#define USB_IN_PIN_SOURCE         GPIO_PIN_SOURCE7
#define USB_IN_PORT_SOURCE        GPIOA_PORT_SOURCE
#define USB_IN_RT_PIN             7   /* PA7 */

/* Pin20 PB12  MCU_FALL  水浸信号输入 */
#define FALL_KEY_PORT                 GPIOB
#define FALL_KEY_PIN                  GPIO_PIN_12
#define FALL_KEY_CLK                  RCC_APB2_PERIPH_GPIOB
#define FALL_KEY_PIN_SOURCE           GPIO_PIN_SOURCE12
#define FALL_KEY_PORT_SOURCE          GPIOB_PORT_SOURCE
#define FALL_KEY_RT_PIN               28  /* PB12 = 16+12 */

/* -------------------------------------------------------------------------- */
/* 电源使能（输出）                                                           */
/* -------------------------------------------------------------------------- */
/* Pin13 PA6  MCU_EN_5V */
#define EN_5V_PA_POW_PORT                GPIOA
#define EN_5V_PA_POW_PIN                 GPIO_PIN_6
#define EN_5V_PA_POW_CLK                 RCC_APB2_PERIPH_GPIOA

/* Pin27 PA8  MCU_EN_PLNA_POW */
#define EN_PLNA_POW_PORT              GPIOA
#define EN_PLNA_POW_PIN               GPIO_PIN_8
#define EN_PLNA_POW_CLK               RCC_APB2_PERIPH_GPIOA

/* Pin28 PA9  MCU_EN_LNA_GNSS */
#define EN_LNA_POW_GNSS_PORT          GPIOA
#define EN_LNA_POW_GNSS_PIN           GPIO_PIN_9
#define EN_LNA_POW_GNSS_CLK           RCC_APB2_PERIPH_GPIOA

/* Pin37 PA15  MCU_EN_BLE  （默认 JTDI，需释放 JTAG） */
#define EN_BLE_POW_PORT               GPIOA
#define EN_BLE_POW_PIN                GPIO_PIN_15
#define EN_BLE_POW_CLK                RCC_APB2_PERIPH_GPIOA

/* Pin23 PB14  MCU_EN_PGNSS */
#define EN_PGNSS_POW_PORT             GPIOB
#define EN_PGNSS_POW_PIN              GPIO_PIN_14
#define EN_PGNSS_POW_CLK              RCC_APB2_PERIPH_GPIOB

/* Pin24 PB15  MCU_EN_LNA_RDSS */
#define EN_LNA_RDSS_POW_PORT          GPIOB
#define EN_LNA_RDSS_POW_PIN           GPIO_PIN_15
#define EN_LNA_RDSS_POW_CLK           RCC_APB2_PERIPH_GPIOB

/* Pin38 PB3  MCU_EN_PRDSS  （默认 JTDO，需释放 JTAG） */
#define EN_PRDSS_POW_PORT             GPIOB
#define EN_PRDSS_POW_PIN              GPIO_PIN_3
#define EN_PRDSS_POW_CLK              RCC_APB2_PERIPH_GPIOB

/* -------------------------------------------------------------------------- */
/* LED（电池电量指示；低电平点亮）                                            */
/* -------------------------------------------------------------------------- */
/* Pin44 PB6  LED1  <30%  active-low */
#define LED1_PORT                 GPIOB
#define LED1_PIN                  GPIO_PIN_6
#define LED1_CLK                  RCC_APB2_PERIPH_GPIOB

/* Pin45 PB7  LED2  30%~70%  active-low */
#define LED2_PORT                 GPIOB
#define LED2_PIN                  GPIO_PIN_7
#define LED2_CLK                  RCC_APB2_PERIPH_GPIOB

/* Pin15 PB2  LED3  70%~100%  active-low */
#define LED3_PORT                 GPIOB
#define LED3_PIN                  GPIO_PIN_2
#define LED3_CLK                  RCC_APB2_PERIPH_GPIOB

/* -------------------------------------------------------------------------- */
/* UART：GNSS = USART3（PB10/PB11，默认脚，无需 remap）                        */
/* -------------------------------------------------------------------------- */
#define GNSS_UART                 USART3
#define GNSS_UART_CLK             RCC_APB1_PERIPH_USART3
#define GNSS_UART_IRQn            USART3_IRQn

#define GNSS_UART_TX_PORT         GPIOB
#define GNSS_UART_TX_PIN          GPIO_PIN_10
#define GNSS_UART_TX_CLK          RCC_APB2_PERIPH_GPIOB

#define GNSS_UART_RX_PORT         GPIOB
#define GNSS_UART_RX_PIN          GPIO_PIN_11
#define GNSS_UART_RX_CLK          RCC_APB2_PERIPH_GPIOB

/* -------------------------------------------------------------------------- */
/* UART：RDSS = USART2（PB4/PB5，需 GPIO_RMP3_USART2）                         */
/* -------------------------------------------------------------------------- */
#define RDSS_UART                 USART2
#define RDSS_UART_CLK             RCC_APB1_PERIPH_USART2
#define RDSS_UART_IRQn            USART2_IRQn
#define RDSS_UART_REMAP           GPIO_RMP3_USART2

#define RDSS_UART_TX_PORT         GPIOB
#define RDSS_UART_TX_PIN          GPIO_PIN_4
#define RDSS_UART_TX_CLK          RCC_APB2_PERIPH_GPIOB

#define RDSS_UART_RX_PORT         GPIOB
#define RDSS_UART_RX_PIN          GPIO_PIN_5
#define RDSS_UART_RX_CLK          RCC_APB2_PERIPH_GPIOB

/* -------------------------------------------------------------------------- */
/* USB（PA11/PA12，由 USB 外设接管）                                          */
/* -------------------------------------------------------------------------- */
#define USB_DM_PORT               GPIOA
#define USB_DM_PIN                GPIO_PIN_11
#define USB_DP_PORT               GPIOA
#define USB_DP_PIN                GPIO_PIN_12
#define USB_GPIO_CLK              RCC_APB2_PERIPH_GPIOA

/* -------------------------------------------------------------------------- */
/* 便捷操作（推挽输出高/低；读输入）                                          */
/* -------------------------------------------------------------------------- */
#define PIN_SET(port, pin)        GPIO_SetBits((port), (pin))
#define PIN_RESET(port, pin)      GPIO_ResetBits((port), (pin))
#define PIN_WRITE(port, pin, on)  do { \
        if (on) GPIO_SetBits((port), (pin)); \
        else    GPIO_ResetBits((port), (pin)); \
    } while (0)
#define PIN_READ(port, pin)       GPIO_ReadInputDataBit((port), (pin))

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_PINS_H__ */
