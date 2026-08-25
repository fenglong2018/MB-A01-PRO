/**
 * @file iwdg.c
 * @brief IWDG：LSI /256，重装 0xFFF ≈ 26s（LSI 按 40kHz）
 */
#include "iwdg.h"
#include "product_config.h"
#include "n32wb452.h"
#include "n32wb452_iwdg.h"
#include "n32wb452_rcc.h"
#include "n32wb452_pwr.h"

#ifndef USE_IWDG
#define USE_IWDG 0
#endif

#if USE_IWDG

static uint8_t s_on;

void iwdg_start(void)
{
    if (s_on)
    {
        IWDG_ReloadKey();
        return;
    }

    RCC_EnableLsi(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_LSIRD) == RESET)
    {
    }

    IWDG_WriteConfig(IWDG_WRITE_ENABLE);
    IWDG_SetPrescalerDiv(IWDG_PRESCALER_DIV256);
    IWDG_CntReload(0x0FFFu);
    IWDG_WriteConfig(IWDG_WRITE_DISABLE);
    IWDG_ReloadKey();
    IWDG_Enable();
    s_on = 1;
}

void iwdg_feed(void)
{
    if (s_on)
    {
        IWDG_ReloadKey();
    }
}

int iwdg_is_started(void)
{
    return s_on ? 1 : 0;
}

void iwdg_stop2_quiet(void)
{
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_PWR, ENABLE);
    PWR->CTRL2 &= (uint16_t) ~(PWR_CTRL2_IWDGWPEN | PWR_CTRL2_IWDGRSTEN);
}

#else

void iwdg_start(void)
{
}

void iwdg_feed(void)
{
}

int iwdg_is_started(void)
{
    return 0;
}

void iwdg_stop2_quiet(void)
{
}

#endif
