/**
 * @file pm_stop0.c
 * @brief idle hook → STOP0；锁计数禁止入睡；时钟恢复交给 board
 */
#include <rthw.h>
#include <rtthread.h>

#include "n32wb452_pwr.h"
#include "n32wb452_rcc.h"
#include "product_config.h"
#include "pm.h"
#include "board_clock.h"

#if USE_PM

static volatile uint32_t s_lock;
static uint8_t s_hook_on;

void pm_lock(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    s_lock++;
    rt_hw_interrupt_enable(level);
}

void pm_unlock(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (s_lock > 0u)
    {
        s_lock--;
    }
    rt_hw_interrupt_enable(level);
}

uint32_t pm_lock_count(void)
{
    return s_lock;
}

static void pm_enter_stop0(void)
{
    /* 确保 PWR 时钟开着（STOP 前后都需要） */
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_PWR, ENABLE);

    /*
     * STOP0：Regulator low-power + WFI。
     * 醒后时钟为 HSI，由 board_clock_resume_after_stop 恢复 HSE/PLL。
     */
    PWR_EnterStopState(PWR_REGULATOR_LOWPOWER, PWR_STOPENTRY_WFI);

    board_clock_resume_after_stop();
}

static void pm_idle_hook(void)
{
    rt_base_t level;

    if (s_lock != 0u)
    {
        return;
    }

    level = rt_hw_interrupt_disable();
    if (s_lock != 0u)
    {
        rt_hw_interrupt_enable(level);
        return;
    }

    pm_enter_stop0();
    rt_hw_interrupt_enable(level);
}

int pm_idle_hook_install(void)
{
    if (s_hook_on)
    {
        return 0;
    }
    if (rt_thread_idle_sethook(pm_idle_hook) != RT_EOK)
    {
        rt_kprintf("[PM] idle hook install fail\n");
        return -1;
    }
    s_hook_on = 1;
    rt_kprintf("[PM] idle STOP0 hook on\n");
    return 0;
}

static int app_pm_init(void)
{
    return pm_idle_hook_install();
}
INIT_APP_EXPORT(app_pm_init);

#else /* !USE_PM */

void pm_lock(void)
{
}

void pm_unlock(void)
{
}

uint32_t pm_lock_count(void)
{
    return 0u;
}

int pm_idle_hook_install(void)
{
    return 0;
}

#endif /* USE_PM */
