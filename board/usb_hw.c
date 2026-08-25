/**
 * @file usb_hw.c
 * @brief N32WB452 USB 时钟/中断；对齐官方 Virtual_COM_Port
 *
 * PA11=USBDM、PA12=USBDP（手册固定脚）。USB 工作时不要配成普通 GPIO。
 * 关机须模拟输入 + 关内部上拉，否则 STOP2 仍约 1.6mA。
 * DP 内部上拉在 0x40001820，写之前必须开 PWR 时钟。
 */
#include "hw_config.h"
#include "usb_lib.h"
#include "usb_pwr.h"
#include "board_pins.h"
#include "n32wb452_rcc.h"
#include "n32wb452_gpio.h"
#include "n32wb452_exti.h"
#include "misc.h"

static void usb_pins_analog(void)
{
    GPIO_InitType gpio;

    RCC_EnableAPB2PeriphClk(USB_GPIO_CLK, ENABLE);
    GPIO_InitStruct(&gpio);
    gpio.Pin        = USB_DM_PIN | USB_DP_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AIN;
    GPIO_InitPeripheral(USB_DM_PORT, &gpio);
}

void USB_Interrupts_Config(void)
{
    NVIC_InitType NVIC_InitStructure;
    EXTI_InitType EXTI_InitStructure;

    NVIC_InitStructure.NVIC_IRQChannel                   = USB_LP_CAN1_RX0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel                   = USBWakeUp_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    EXTI_ClrITPendBit(EXTI_LINE18);
    EXTI_InitStruct(&EXTI_InitStructure);
    EXTI_InitStructure.EXTI_Line    = EXTI_LINE18;
    EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_InitPeripheral(&EXTI_InitStructure);
}

void Set_USBClock(void)
{
    uint32_t n;

    /* SystemInit 写完 PWR_CTRL3 会关掉 PWR 钟；内部 DP 上拉寄存器依赖它 */
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_PWR, ENABLE);

    /* USB FS 必须 PLL 48M。HSE 失败时不要死等 USB 线程。 */
    n = 0u;
    while ((RCC_GetSysclkSrc() != 0x08) && (n < 1000000u))
    {
        n++;
    }

    /* HSE 32M → PLL 144M / 3 = 48M */
    RCC_ConfigUsbClk(RCC_USBCLK_SRC_PLLCLK_DIV3);
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_USB, ENABLE);
}

void usb_hw_deinit(void)
{
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_PWR | RCC_APB1_PERIPH_USB, ENABLE);
    _DisPortPullup();
    PowerOff();
    NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
    NVIC_DisableIRQ(USBWakeUp_IRQn);
    usb_pins_analog();
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_USB, DISABLE);
}

void Enter_LowPowerMode(void)
{
    bDeviceState = SUSPENDED;
}

void Leave_LowPowerMode(void)
{
    USB_DeviceMess *pInfo = &Device_Info;

    if (pInfo->CurrentConfiguration != 0)
    {
        bDeviceState = CONFIGURED;
    }
    else
    {
        bDeviceState = ATTACHED;
    }
}

void USART_Config_Default(void)
{
    /* 不配 USART1：PA9 是 GNSS LNA 使能 */
}

bool USART_Config(void)
{
    return true;
}

void Get_SerialNum(void)
{
}
