/**
 * @file board_clock.c
 * @brief STOP 唤醒后时钟恢复：HSE 32M / 2 × 9 → PLL 144M，USB=PLL/3
 */
#include <rtthread.h>
#include "n32wb452.h"
#include "n32wb452_rcc.h"
#include "board_clock.h"
#include "product_config.h"

#if USE_USB_CDC
#include "usb_hw.h"
#endif

#ifndef SYSCLK_FREQ
#define SYSCLK_FREQ 144000000u
#endif

void board_clock_resume_after_stop(void)
{
    ErrorStatus hse_ok;
    uint32_t ac;

    RCC_ConfigHse(RCC_HSE_ENABLE);
    hse_ok = RCC_WaitHseStable();
    if (hse_ok != SUCCESS)
    {
        SystemCoreClock = HSI_VALUE;
        SysTick_Config(SystemCoreClock / RT_TICK_PER_SECOND);
        return;
    }

    ac = FLASH->AC;
    ac &= (uint32_t)~FLASH_AC_LATENCY;
    ac |= (uint32_t)((SYSCLK_FREQ - 1u) / 32000000u);
    FLASH->AC = ac;

    RCC_ConfigHclk(RCC_SYSCLK_DIV1);
    RCC_ConfigPclk2(RCC_HCLK_DIV2);
    RCC_ConfigPclk1(RCC_HCLK_DIV4);

    RCC_ConfigPll(RCC_PLL_SRC_HSE_DIV2, RCC_PLL_MUL_9);
    RCC_EnablePll(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_PLLRD) == RESET)
    {
    }

    RCC_ConfigSysclk(RCC_SYSCLK_SRC_PLLCLK);
    while ((RCC->CFG & RCC_CFG_SCLKSTS) != (uint32_t)0x08)
    {
    }

    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / RT_TICK_PER_SECOND);

#if USE_USB_CDC
    Set_USBClock();
#endif
}
