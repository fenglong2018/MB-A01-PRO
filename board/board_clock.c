/**
 * @file board_clock.c
 * @brief 系统时钟：优先 HSE 32M / 2 × 9 → PLL 144M；HSE 不起振时退 HSI/2 × 18 → PLL 72M
 *
 * USB 必须拿到 48M。144M 走 PLL/3，72M 走 PLL/1.5，两条路都成立。
 * 关键是任何情况下 SYSCLK 都得挂在 PLL 上：SystemInit 里 HSE 失败只会退到
 * 裸 HSI 8M 且 PLL 全关，那时 USB 分频器没有输入，枚举必然失败。
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

#define SYSCLK_SRC_PLL  0x08u

static uint8_t s_hse_ok = 1u;

int board_clock_hse_ok(void)
{
    return s_hse_ok ? 1 : 0;
}

static void flash_latency_for(uint32_t hz)
{
    uint32_t ac = FLASH->AC;
    ac &= (uint32_t)~FLASH_AC_LATENCY;
    ac |= (uint32_t)((hz - 1u) / 32000000u);
    FLASH->AC = ac;
}

uint32_t board_clock_pll_hz(void)
{
    uint32_t mul;
    uint32_t in;

    if (RCC_GetFlagStatus(RCC_FLAG_PLLRD) == RESET)
    {
        return 0u;
    }

    mul = RCC->CFG & RCC_CFG_PLLMULFCT;
    if ((mul & RCC_CFG_PLLMULFCT_4) == 0u)
    {
        mul = (mul >> 18) + 2u;
    }
    else
    {
        mul = ((mul >> 18) - 496u) + 1u;
    }

    if ((RCC->CFG & RCC_CFG_PLLSRC) == 0u)
    {
        in = HSI_VALUE / 2u;
    }
    else if ((RCC->CFG & RCC_CFG_PLLHSEPRES) != 0u)
    {
        in = HSE_VALUE / 2u;
    }
    else
    {
        in = HSE_VALUE;
    }

    return in * mul;
}

/** HSE 起不来时的兜底：HSI 8M / 2 × 18 = 72M，USB 取 PLL/1.5 */
static void clock_to_hsi_pll72(void)
{
    RCC_EnableHsi(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_HSIRD) == RESET)
    {
    }

    RCC_ConfigSysclk(RCC_SYSCLK_SRC_HSI);
    while (RCC_GetSysclkSrc() != 0x00u)
    {
    }

    RCC_EnablePll(DISABLE);
    flash_latency_for(72000000u);

    RCC_ConfigHclk(RCC_SYSCLK_DIV1);
    RCC_ConfigPclk2(RCC_HCLK_DIV1);
    RCC_ConfigPclk1(RCC_HCLK_DIV2);

    RCC_ConfigPll(RCC_PLL_SRC_HSI_DIV2, RCC_PLL_MUL_18);
    RCC_EnablePll(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_PLLRD) == RESET)
    {
    }

    RCC_ConfigSysclk(RCC_SYSCLK_SRC_PLLCLK);
    while (RCC_GetSysclkSrc() != SYSCLK_SRC_PLL)
    {
    }

    SystemCoreClockUpdate();
}

static ErrorStatus clock_to_hse_pll144(void)
{
    RCC_ConfigHse(RCC_HSE_ENABLE);
    if (RCC_WaitHseStable() != SUCCESS)
    {
        return ERROR;
    }

    RCC_EnablePll(DISABLE);
    flash_latency_for(SYSCLK_FREQ);

    RCC_ConfigHclk(RCC_SYSCLK_DIV1);
    RCC_ConfigPclk2(RCC_HCLK_DIV2);
    RCC_ConfigPclk1(RCC_HCLK_DIV4);

    RCC_ConfigPll(RCC_PLL_SRC_HSE_DIV2, RCC_PLL_MUL_9);
    RCC_EnablePll(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_PLLRD) == RESET)
    {
    }

    RCC_ConfigSysclk(RCC_SYSCLK_SRC_PLLCLK);
    while (RCC_GetSysclkSrc() != SYSCLK_SRC_PLL)
    {
    }

    SystemCoreClockUpdate();
    return SUCCESS;
}

void board_clock_ensure_pll(void)
{
    if (RCC_GetSysclkSrc() == SYSCLK_SRC_PLL)
    {
        s_hse_ok = ((RCC->CFG & RCC_CFG_PLLSRC) != 0u) ? 1u : 0u;
        return;
    }

    /* 走到这里说明 SystemInit 里 HSE 没起来，现在是裸 HSI 且 PLL 关着 */
    s_hse_ok = 0u;
    clock_to_hsi_pll72();
}

void board_clock_resume_after_stop(void)
{
    if (clock_to_hse_pll144() != SUCCESS)
    {
        s_hse_ok = 0u;
        clock_to_hsi_pll72();
    }
    else
    {
        s_hse_ok = 1u;
    }

    SysTick_Config(SystemCoreClock / RT_TICK_PER_SECOND);

#if USE_USB_CDC
    Set_USBClock();
#endif
}
