/**
 * @file usb_hw.c
 * @brief N32WB452 USB 时钟/中断
 *
 * 对齐官方 Virtual_COM_Port：
 * - 从不 GPIO_Init PA11/PA12（USBDM/USBDP 由 PHY 占用）
 * - Set_USBClock 只配 PLL/3 并开 USB 钟
 * - USB_LP 抢占 1；不改 NVIC 分组（board 已是 Group_4）
 */
#include "hw_config.h"
#include "usb_lib.h"
#include "usb_pwr.h"
#include "n32wb452_rcc.h"
#include "n32wb452_gpio.h"
#include "n32wb452_exti.h"
#include "board_clock.h"
#include "misc.h"

volatile uint32_t g_usb_irq_cnt;
volatile uint32_t g_usb_reset_cnt;
volatile uint32_t g_usb_ctr_cnt;
volatile uint32_t g_usb_sof_cnt;
volatile uint32_t g_usb_err_cnt;
volatile uint32_t g_usb_desc_cnt;
volatile uint32_t g_usb_last_req;

void RESET_Callback(void)
{
    g_usb_reset_cnt++;
}

void ERR_Callback(void)
{
    g_usb_err_cnt++;
}

void CTR_Callback(void)
{
    g_usb_ctr_cnt++;
    g_usb_last_req = ((uint32_t)pInformation->bmRequestType << 24)
                   | ((uint32_t)pInformation->bRequest << 16)
                   | (uint32_t)pInformation->USBwValue;
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
    uint32_t pll;

    /* SystemInit 写完 PWR_CTRL3 会关掉 PWR 钟；内部 DP 上拉寄存器依赖它 */
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_PWR, ENABLE);

    /* HSE 没起振时 SystemInit 会退到裸 HSI 且不开 PLL，先把 PLL 补上 */
    board_clock_ensure_pll();

    /*
     * 分频器按实际 PLL 频率选，不能写死 /3：
     * HSE 路 144M → /3，HSI 兜底路 72M → /1.5，都得到 48M。
     */
    pll = board_clock_pll_hz();
    if (pll == 144000000u)
    {
        RCC_ConfigUsbClk(RCC_USBCLK_SRC_PLLCLK_DIV3);
    }
    else if (pll == 96000000u)
    {
        RCC_ConfigUsbClk(RCC_USBCLK_SRC_PLLCLK_DIV2);
    }
    else if (pll == 72000000u)
    {
        RCC_ConfigUsbClk(RCC_USBCLK_SRC_PLLCLK_DIV1_5);
    }
    else if (pll == 48000000u)
    {
        RCC_ConfigUsbClk(RCC_USBCLK_SRC_PLLCLK_DIV1);
    }
    else
    {
        /* 拿不到 48M，开了 USB 钟也只会枚举失败 */
        return;
    }

    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_USB, ENABLE);
}

void usb_hw_deinit(void)
{
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_PWR | RCC_APB1_PERIPH_USB, ENABLE);
    _DisPortPullup();
    PowerOff();
    NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
    NVIC_DisableIRQ(USBWakeUp_IRQn);
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
