/**
 * @file pm_stop0.c
 * @brief idle hook → STOP0；锁计数禁止入睡；时钟恢复交给 board
 */
#include <rthw.h>
#include <rtthread.h>

#include "n32wb452_pwr.h"
#include "n32wb452_rcc.h"
#include "n32wb452_bkp.h"
#include "n32wb452.h"
#include "product_config.h"
#include "pm.h"
#include "board_clock.h"
#include "board_gpio.h"
#if USE_USB_CDC
#include "cdc_io.h"
#endif

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

static int pm_usb_hold_awake(void)
{
    if (board_usb_inserted())
    {
        return 1;
    }
#if USE_USB_CDC
    /* PA7 抖动时 CDC 可能已拉 DP；只看脚会 STOP0 把 48M 掐掉，主机无法识别 */
    if (usb_cdc_is_on())
    {
        return 1;
    }
#endif
    return 0;
}

static void pm_idle_hook(void)
{
    rt_base_t level;

    /* STOP0 停 PLL，USB 48M(PLL/3) 不能单独留；有线或 CDC 已起则不睡 */
    if ((s_lock != 0u) || pm_usb_hold_awake())
    {
        return;
    }

    level = rt_hw_interrupt_disable();
    if ((s_lock != 0u) || pm_usb_hold_awake())
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

static uint32_t s_rram_stk[64] __attribute__((section(".rram"), used));
#define PM_WAKE_BKP_MAGIC       BOARD_STOP2_WAKE_MAGIC

__attribute__((noinline, noreturn))
static void pm_stop2_on_rram(void)
{
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_PWR, ENABLE);
    PWR->CTRL2 |= (uint16_t)(PWR_CTRL2_STOP2S | PWR_CTRL2_SR2VBRET | PWR_CTRL2_SR2STBRET);
    PWR_EnterSTOP2Mode(PWR_STOPENTRY_WFI);
    /*
     * RETRAM 在软件复位后不可靠。BKP 跨复位还在。
     * 只盖戳，短/长由复位后 KEY 轮询。须在 RTC 可能 BackupReset 之前被 early_init 读走。
     */
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_PWR | RCC_APB1_PERIPH_BKP, ENABLE);
    PWR_BackupAccessEnable(ENABLE);
    BKP_WriteBkpData(BKP_DAT42, PM_WAKE_BKP_MAGIC);
    NVIC_SystemReset();
    while (1)
    {
    }
}

void pm_stop2_enter(void)
{
    uint32_t top = (uint32_t)&s_rram_stk[64];

    rt_kprintf("[PM] STOP2 (true off)\n");
    __disable_irq();
    /* kprintf 期间 LED 线程可能又点亮；关中断后再拉灭，STOP2 保持此电平 */
    board_gpio_outputs_off();
    __asm volatile(
        "msr msp, %0\n"
        "msr psp, %0\n"
        "bx  %1\n"
        :
        : "r"(top), "r"(pm_stop2_on_rram)
        : "memory");
    while (1)
    {
    }
}

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

void pm_stop2_enter(void)
{
}

#endif /* USE_PM */
