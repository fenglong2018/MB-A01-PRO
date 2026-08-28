/**
 * @file key.c
 * @brief 四路 IO：EXTI 叫醒并关该路中断 → 10ms 轮询积分 → MODE
 *
 * USB / SIM / FALL：有效 +1、无效 −1，反相不清零。确认有效后保持关中断只轮询。
 * SOS：按下同样积分；松手连续 SOS_REL_CNT 拍才确认（避免短按晚报）。
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include "config.h"
#include "key.h"
#include "board_pins.h"
#include "board_gpio.h"
#include "mode.h"
#if USE_PM
#include "pm.h"
#endif

#if USE_KEY

enum
{
    HOLD_NONE = 0,
    HOLD_IN   = 1,
    HOLD_OUT  = 2
};

/** USB / SIM / FALL 共用：确认在位后关 IRQ 只轮询 */
typedef struct
{
    rt_base_t pin;
    int8_t    on_level;
    uint8_t   in_th;
    uint8_t   out_th;
    uint8_t   run;
    uint8_t   held;
    uint8_t   in_cnt;
    uint8_t   out_cnt;
    uint8_t   idle;
} hold_t;

/** SOS：按下积分；松手另计 */
typedef struct
{
    uint8_t   run;
    uint16_t  press;
    uint16_t  peak;
    uint8_t   rel;
} sos_det_t;

static struct rt_semaphore s_key_sem;
static volatile rt_uint32_t s_key_pending;
static struct rt_thread s_key_thread;
static rt_uint8_t s_key_stack[KEY_THREAD_STACK];

static sos_det_t s_sos;
static uint8_t s_sos_long_done;
static uint8_t s_sos_boot_press;

static hold_t s_usb;
static hold_t s_sim;
static hold_t s_fall;
static uint8_t s_pm_key;

static rt_base_t key_id_to_pin(board_key_id_t id)
{
    switch (id)
    {
    case BOARD_KEY_SOS:    return SOS_KEY_RT_PIN;
    case BOARD_KEY_USB_IN: return USB_IN_RT_PIN;
    case BOARD_KEY_FALL:   return FALL_KEY_RT_PIN;
    case BOARD_KEY_SIM:    return RD_BD_SIMCARD_RT_PIN;
    default:               return -1;
    }
}

int sim_present(void)
{
    return s_sim.held ? 1 : 0;
}

int usb_present(void)
{
    return s_usb.held ? 1 : 0;
}

static void hold_clear(hold_t *h)
{
    h->run = 0;
    h->held = 0;
    h->in_cnt = 0;
    h->out_cnt = 0;
    h->idle = 0;
}

static void hold_cfg(hold_t *h, rt_base_t pin, int on_level,
                     uint8_t in_th, uint8_t out_th)
{
    h->pin = pin;
    h->on_level = (int8_t)on_level;
    h->in_th = in_th;
    h->out_th = out_th;
    hold_clear(h);
}

static void hold_start(hold_t *h)
{
    rt_pin_mode(h->pin, PIN_MODE_INPUT);
    h->run = 1;
    h->held = 0;
    h->in_cnt = 0;
    h->out_cnt = 0;
    h->idle = 0;
}

static void hold_rearm(hold_t *h)
{
    rt_pin_mode(h->pin, PIN_MODE_INPUT);
    rt_pin_irq_enable(h->pin, PIN_IRQ_ENABLE);
    hold_clear(h);
}

static void hold_enter(hold_t *h)
{
    if (h->run)
    {
        return;
    }
    hold_start(h);
}

static int hold_poll(hold_t *h)
{
    int on;

    if (!h->run)
    {
        return HOLD_NONE;
    }

    on = (rt_pin_read(h->pin) == (int)h->on_level) ? 1 : 0;

    if (on)
    {
        h->idle = 0;
        if (h->held)
        {
            if (h->out_cnt > 0u)
            {
                h->out_cnt--;
            }
            return HOLD_NONE;
        }
        if (h->in_cnt < h->in_th)
        {
            h->in_cnt++;
        }
        if (h->in_cnt >= h->in_th)
        {
            h->held = 1;
            h->out_cnt = 0;
            return HOLD_IN;
        }
        return HOLD_NONE;
    }

    if (h->held)
    {
        if (h->out_cnt < h->out_th)
        {
            h->out_cnt++;
        }
        if (h->out_cnt >= h->out_th)
        {
            h->held = 0;
            h->in_cnt = 0;
            h->out_cnt = 0;
            hold_rearm(h);
            return HOLD_OUT;
        }
        return HOLD_NONE;
    }

    if (h->in_cnt > 0u)
    {
        h->in_cnt--;
        h->idle = 0;
        return HOLD_NONE;
    }
    if (h->idle < 0xFFu)
    {
        h->idle++;
    }
    if (h->idle >= h->in_th)
    {
        hold_rearm(h);
    }
    return HOLD_NONE;
}

static int sos_is_active(void)
{
    return (rt_pin_read(SOS_KEY_RT_PIN) == PIN_LOW);
}

static void sos_clear(void)
{
    s_sos.run = 0;
    s_sos.press = 0;
    s_sos.peak = 0;
    s_sos.rel = 0;
}

static void sos_rearm(void)
{
    rt_pin_mode(SOS_KEY_RT_PIN, PIN_MODE_INPUT);
    rt_pin_irq_enable(SOS_KEY_RT_PIN, PIN_IRQ_ENABLE);
    sos_clear();
    s_sos_long_done = 0;
    s_sos_boot_press = 0;
}

static void sos_on_short(void)
{
    rt_kprintf("[KEY] SOS short\n");
    mode_post_event(MODE_EVT_SOS_SHORT);
}

static void sos_on_long(void)
{
    rt_kprintf("[KEY] SOS long\n");
    mode_post_event(MODE_EVT_SOS_LONG);
}

static void sos_enter(void)
{
    if (s_sos.run)
    {
        return;
    }
    rt_pin_mode(SOS_KEY_RT_PIN, PIN_MODE_INPUT);
    s_sos.run = 1;
    s_sos.press = 0;
    s_sos.peak = 0;
    s_sos.rel = 0;
    s_sos_long_done = 0;
}

static void sos_sample_poll(void)
{
    int active;

    if (!s_sos.run)
    {
        return;
    }

    active = sos_is_active();
    if (active)
    {
        s_sos.rel = 0;
        if (s_sos.press < SOS_LONG_CNT)
        {
            s_sos.press++;
        }
        if (s_sos.press > s_sos.peak)
        {
            s_sos.peak = s_sos.press;
        }
        if (!s_sos_long_done && (s_sos.press >= SOS_LONG_CNT))
        {
            s_sos_long_done = 1;
            sos_on_long();
        }
        return;
    }

    if (s_sos.press > 0u)
    {
        s_sos.press--;
    }
    if (s_sos.rel < 0xFFu)
    {
        s_sos.rel++;
    }
    if (s_sos.rel < SOS_REL_CNT)
    {
        return;
    }

    /* 连续约 80ms 无效：确认松开。短按看 peak，避免松手减 8 拍把 24 减没 */
    if (s_sos_boot_press || s_sos_long_done)
    {
        sos_rearm();
        return;
    }
    if (s_sos.peak >= SOS_SHORT_CNT)
    {
        sos_on_short();
    }
    sos_rearm();
}

static void usb_enter(void)
{
    hold_enter(&s_usb);
}

static void usb_sample_poll(void)
{
    int ev = hold_poll(&s_usb);

    if (ev == HOLD_IN)
    {
        rt_kprintf("[KEY] USB_IN\n");
        mode_post_event(MODE_EVT_USB_IN);
    }
    else if (ev == HOLD_OUT)
    {
        rt_kprintf("[KEY] USB_OUT\n");
        mode_post_event(MODE_EVT_USB_OUT);
    }
}

static void sim_enter(void)
{
    hold_enter(&s_sim);
}

static void sim_sample_poll(void)
{
    int ev = hold_poll(&s_sim);

    if (ev == HOLD_IN)
    {
        rt_kprintf("[KEY] SIM_IN (card present)\n");
        mode_post_event(MODE_EVT_SIM);
    }
    else if (ev == HOLD_OUT)
    {
        rt_kprintf("[KEY] SIM_OUT (no card)\n");
        mode_post_event(MODE_EVT_SIM);
    }
}

static void fall_enter(void)
{
    hold_enter(&s_fall);
}

static void fall_sample_poll(void)
{
    int ev = hold_poll(&s_fall);

    if (ev == HOLD_IN)
    {
        rt_kprintf("[KEY] FALL confirmed\n");
        mode_post_event(MODE_EVT_FALL);
    }
}

static void key_isr_hook(board_key_id_t id, void *user)
{
    rt_base_t pin;

    (void)user;
    s_key_pending |= (1u << (rt_uint32_t)id);
    rt_sem_release(&s_key_sem);

    pin = key_id_to_pin(id);
    if (pin >= 0)
    {
        rt_pin_irq_enable(pin, PIN_IRQ_DISABLE);
    }
}

static rt_uint32_t key_pending_take(void)
{
    rt_base_t level;
    rt_uint32_t pending;

    level = rt_hw_interrupt_disable();
    pending = s_key_pending;
    s_key_pending = 0;
    rt_hw_interrupt_enable(level);
    return pending;
}

static void key_handle_irq_events(rt_uint32_t pending)
{
    if (pending & KEY_EVT_SOS)
    {
        sos_enter();
    }
    if (pending & KEY_EVT_USB_IN)
    {
        usb_enter();
    }
    if (pending & KEY_EVT_FALL)
    {
        fall_enter();
    }
    if (pending & KEY_EVT_SIM)
    {
        sim_enter();
    }
}

int key_is_busy(void)
{
    if (s_key_pending != 0u)
    {
        return 1;
    }
    /* 已确认 USB/SIM 在位只轮询，不挡 STOP2；插入过程仍算忙 */
    if (s_sos.run || s_fall.run)
    {
        return 1;
    }
    if ((s_usb.run && !s_usb.held) || (s_sim.run && !s_sim.held))
    {
        return 1;
    }
    if (sos_is_active())
    {
        return 1;
    }
    return 0;
}

static void key_pm_sync(void)
{
#if USE_PM
    int busy = key_is_busy();

    if (busy && !s_pm_key)
    {
        pm_lock();
        s_pm_key = 1;
    }
    else if (!busy && s_pm_key)
    {
        pm_unlock();
        s_pm_key = 0;
    }
#endif
}

static void key_take_boot_level(void)
{
    int sos_down = board_boot_sos_down();
    int fall_on  = board_boot_fall_active();

    if (sos_down)
    {
        rt_pin_irq_enable(SOS_KEY_RT_PIN, PIN_IRQ_DISABLE);
        sos_enter();
        s_sos_boot_press = 1;
        rt_kprintf("[KEY] SOS held at reset (long only)\n");
    }
    if (fall_on)
    {
        rt_pin_irq_enable(FALL_KEY_RT_PIN, PIN_IRQ_DISABLE);
        fall_enter();
        rt_kprintf("[KEY] FALL active at reset\n");
    }

    /*
     * USB / SIM 是电平：上电或 STOP2 复位时已经插着则没有沿。
     * 积分确认插入才报 IN；未插入积满 idle 只 rearm，不上报 OUT。
     */
    rt_pin_irq_enable(USB_IN_RT_PIN, PIN_IRQ_DISABLE);
    usb_enter();
    rt_pin_irq_enable(RD_BD_SIMCARD_RT_PIN, PIN_IRQ_DISABLE);
    sim_enter();
}

static void key_thread_entry(void *param)
{
    (void)param;

    while (1)
    {
        if (rt_sem_take(&s_key_sem, rt_tick_from_millisecond(KEY_POLL_MS)) == RT_EOK)
        {
            while (rt_sem_trytake(&s_key_sem) == RT_EOK)
            {
            }
            key_handle_irq_events(key_pending_take());
        }

        sos_sample_poll();
        usb_sample_poll();
        sim_sample_poll();
        fall_sample_poll();
        key_pm_sync();
    }
}

int key_init(void)
{
    rt_err_t err;

    sos_clear();
    s_sos_long_done = 0;
    s_sos_boot_press = 0;

    hold_cfg(&s_usb, USB_IN_RT_PIN, USB_IN_INSERTED_LEVEL,
             USB_IN_CONFIRM_CNT, USB_OUT_CONFIRM_CNT);
    hold_cfg(&s_sim, RD_BD_SIMCARD_RT_PIN, PIN_LOW,
             SIM_IN_CONFIRM_CNT, SIM_OUT_CONFIRM_CNT);
    hold_cfg(&s_fall, FALL_KEY_RT_PIN, PIN_HIGH,
             FALL_ON_CONFIRM_CNT, FALL_OFF_CONFIRM_CNT);

#if USE_KEY_IRQ
    if (board_key_irq_init(key_isr_hook, RT_NULL) != 0)
    {
        rt_kprintf("[KEY] irq init failed\n");
        return -1;
    }
#endif

    rt_pin_mode(USB_IN_RT_PIN, PIN_MODE_INPUT);
    rt_pin_mode(RD_BD_SIMCARD_RT_PIN, PIN_MODE_INPUT);
    rt_pin_mode(FALL_KEY_RT_PIN, PIN_MODE_INPUT);
    rt_pin_mode(SOS_KEY_RT_PIN, PIN_MODE_INPUT);

    rt_sem_init(&s_key_sem, "key", 0, RT_IPC_FLAG_FIFO);

    key_take_boot_level();
    key_pm_sync();

    err = rt_thread_init(&s_key_thread,
                         "key",
                         key_thread_entry,
                         RT_NULL,
                         s_key_stack,
                         sizeof(s_key_stack),
                         KEY_THREAD_PRIO,
                         20);
    if (err != RT_EOK)
    {
        rt_kprintf("[KEY] thread init failed: %d\n", err);
        return -1;
    }

    rt_thread_startup(&s_key_thread);
    rt_kprintf("[KEY] ready poll=%dms usb=%u/%u sim=%u/%u fall=%u/%u sos=%u/%u rel=%u card=%d\n",
               KEY_POLL_MS,
               (unsigned)USB_IN_CONFIRM_CNT, (unsigned)USB_OUT_CONFIRM_CNT,
               (unsigned)SIM_IN_CONFIRM_CNT, (unsigned)SIM_OUT_CONFIRM_CNT,
               (unsigned)FALL_ON_CONFIRM_CNT, (unsigned)FALL_OFF_CONFIRM_CNT,
               (unsigned)SOS_SHORT_CNT, (unsigned)SOS_LONG_CNT,
               (unsigned)SOS_REL_CNT, s_sim.held);
    return 0;
}

static int app_key_init(void)
{
    return key_init();
}
INIT_APP_EXPORT(app_key_init);

#else /* !USE_KEY */

int key_init(void)
{
    return 0;
}

int sim_present(void)
{
    return 1;
}

int usb_present(void)
{
    return board_usb_inserted();
}

int key_is_busy(void)
{
    return 0;
}

#endif /* USE_KEY */
