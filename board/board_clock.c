/**
 * @file board_clock.c
 * @brief STOP 唤醒后时钟恢复（与 system_n32wb452 SetSysClock 目标一致：HSE 32M → PLL 144M）
 */
#include <rtthread.h>
#include "n32wb452.h"
#include "n32wb452_rcc.h"
#include "board_clock.h"
#include "product_config.h"

#if USE_USB_CDC
#include "usb_hw.h"
#endif

#ifndef HSE_VALUE
#define HSE_VALUE 32000000u
#endif

#ifndef SYSCLK_FREQ
#define SYSCLK_FREQ 144000000u
#endif

/* HSE/2 * 9 = 144M（与 system_n32wb452.c 在 HSE=32M 时一致） */
#define PM_PLL_MUL  RCC_PLL_MUL_9

void board_clock_resume_after_stop(void)
{
    ErrorStatus hse_ok;
    uint32_t ac;

    /* STOP 醒后默认 HSI；重新拉起 HSE */
    RCC_ConfigHse(RCC_HSE_ENABLE);
    hse_ok = RCC_WaitHseStable();
    if (hse_ok != SUCCESS)
    {
        SystemCoreClock = HSI_VALUE;
        SysTick_Config(SystemCoreClock / RT_TICK_PER_SECOND);
        return;
    }

    /* Flash wait：144M → latency 4（同 SystemInit） */
    ac = FLASH->AC;
    ac &= (uint32_t)~FLASH_AC_LATENCY;
    ac |= (uint32_t)((SYSCLK_FREQ - 1u) / 32000000u);
    FLASH->AC = ac;

    RCC_ConfigHclk(RCC_SYSCLK_DIV1);
    RCC_ConfigPclk2(RCC_HCLK_DIV2);
    RCC_ConfigPclk1(RCC_HCLK_DIV4);

    /* PLL: HSE/2 * 9 */
    RCC_ConfigPll(RCC_PLL_SRC_HSE_DIV2, PM_PLL_MUL);
    RCC_EnablePll(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_PLLRD) == RESET)
    {
    }

    RCC_ConfigSysclk(RCC_SYSCLK_SRC_PLLCLK);
    /* SCLKSTS：PLL 为源时为 0x08（与 system_n32wb452.c 一致） */
    while ((RCC->CFG & RCC_CFG_SCLKSTS) != (uint32_t)0x08)
    {
    }

    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / RT_TICK_PER_SECOND);

#if USE_USB_CDC
    Set_USBClock();
#endif
}
