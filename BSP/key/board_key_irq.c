/**
 * @file board_key_irq.c
 * @brief SOS_KEY(PA0) / USB_IN(PA7) / FALL_KEY(PB12) / SIM(PA5) 中断
 *
 * 复用 DeviceDrivers/gpio 的 EXTI 框架（勿再实现 EXTI0/9_5/15_10）。
 * ISR 只转调 hook，业务在 BSP/key 线程中处理。
 */
#include <rtthread.h>
#include "board_key_irq.h"

static board_key_isr_hook_t s_hook;
static void *s_hook_user;

static void sos_key_irq_hdr(void *args)
{
    (void)args;
    if (s_hook)
    {
        s_hook(BOARD_KEY_SOS, s_hook_user);
    }
}

static void usb_in_irq_hdr(void *args)
{
    (void)args;
    if (s_hook)
    {
        s_hook(BOARD_KEY_USB_IN, s_hook_user);
    }
}

static void fall_key_irq_hdr(void *args)
{
    (void)args;
    if (s_hook)
    {
        s_hook(BOARD_KEY_FALL, s_hook_user);
    }
}

static void sim_irq_hdr(void *args)
{
    (void)args;
    if (s_hook)
    {
        s_hook(BOARD_KEY_SIM, s_hook_user);
    }
}

static int board_pin_irq_setup(rt_base_t pin, rt_base_t mode, rt_uint32_t irq_mode,
                               void (*hdr)(void *args), const char *name)
{
    rt_err_t ret;

    rt_pin_mode(pin, mode);

    ret = rt_pin_attach_irq(pin, irq_mode, hdr, RT_NULL);
    if (ret != RT_EOK)
    {
        rt_kprintf("[KEY] %s attach_irq failed: %d\n", name, ret);
        return -1;
    }

    ret = rt_pin_irq_enable(pin, PIN_IRQ_ENABLE);
    if (ret != RT_EOK)
    {
        rt_kprintf("[KEY] %s irq_enable failed: %d\n", name, ret);
        return -1;
    }

    return 0;
}

int board_key_irq_init(board_key_isr_hook_t hook, void *user)
{
    int ok = 0;

    s_hook = hook;
    s_hook_user = user;

    ok |= board_pin_irq_setup(SOS_KEY_RT_PIN, PIN_MODE_INPUT,
                              BOARD_SOS_KEY_IRQ_MODE, sos_key_irq_hdr, "SOS_KEY");
    ok |= board_pin_irq_setup(USB_IN_RT_PIN, PIN_MODE_INPUT,
                              BOARD_USB_IN_IRQ_MODE, usb_in_irq_hdr, "USB_IN");
    ok |= board_pin_irq_setup(FALL_KEY_RT_PIN, PIN_MODE_INPUT,
                              BOARD_FALL_KEY_IRQ_MODE, fall_key_irq_hdr, "FALL_KEY");
    ok |= board_pin_irq_setup(RD_BD_SIMCARD_RT_PIN, PIN_MODE_INPUT,
                              BOARD_SIM_IRQ_MODE, sim_irq_hdr, "SIM");

    return ok;
}
