/**
 * @file key.c
 * @brief 按键：EXTI → 滤波 → mode_post_event
 *
 * SOS：短按/长按互斥；滤波中断一次无效则清零并恢复中断。
 * FALL：连续有效 KEY_FILTER_CNT 次确认。
 * USB：双沿；连续稳定 KEY_FILTER_CNT 次后按电平报 USB_IN/USB_OUT。
 * SIM：双沿；低=有卡 高=无卡；仅日志 + sim_present()，不进 MODE。
 */
#include <rtthread.h>
#include <rthw.h>
#include "config.h"
#include "key.h"
#include "board_pins.h"
#include "mode.h"

#if USE_KEY

typedef enum
{
    SOS_ST_IDLE = 0,
    SOS_ST_SAMPLE,
    SOS_ST_WAIT_REL,
} sos_st_t;

typedef enum
{
    FLT_ST_IDLE = 0,
    FLT_ST_SAMPLE,
    FLT_ST_WAIT_REL,
} flt_st_t;

typedef struct
{
    flt_st_t   st;
    rt_uint8_t hit;
    rt_base_t  pin;
    int        active_low;
    const char *name;
} key_filter_t;

/** USB 双沿：稳定在某一电平后上报 */
typedef struct
{
    flt_st_t   st;
    rt_uint8_t hit;
    int        target_lvl; /* -1 未定 */
    int        last_lvl;   /* 上次已上报电平，-1 表示无 */
} usb_filter_t;

static struct rt_semaphore s_key_sem;
static volatile rt_uint32_t s_key_pending;
static struct rt_thread s_key_thread;
static rt_uint8_t s_key_stack[KEY_THREAD_STACK];

static sos_st_t s_sos_st;
static rt_uint8_t s_sos_hit;
static rt_uint8_t s_sos_long_done;

static key_filter_t s_fall_flt;
static usb_filter_t s_usb_flt;
static usb_filter_t s_sim_flt;
static volatile int s_sim_present = 0; /* 1=有卡（低） */

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
    return s_sim_present ? 1 : 0;
}

static int sos_is_active(void)
{
    return (rt_pin_read(SOS_KEY_RT_PIN) == PIN_LOW);
}

static void sos_rearm(void)
{
    rt_pin_mode(SOS_KEY_RT_PIN, PIN_MODE_INPUT);
    rt_pin_irq_enable(SOS_KEY_RT_PIN, PIN_IRQ_ENABLE);
    s_sos_st = SOS_ST_IDLE;
    s_sos_hit = 0;
    s_sos_long_done = 0;
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

static int flt_is_active(const key_filter_t *f)
{
    int lvl = rt_pin_read(f->pin);

    if (f->active_low)
    {
        return (lvl == PIN_LOW);
    }
    return (lvl != PIN_LOW);
}

static void flt_rearm(key_filter_t *f)
{
    rt_pin_mode(f->pin, PIN_MODE_INPUT);
    rt_pin_irq_enable(f->pin, PIN_IRQ_ENABLE);
    f->st = FLT_ST_IDLE;
    f->hit = 0;
}

static void flt_enter(key_filter_t *f)
{
    rt_pin_mode(f->pin, PIN_MODE_INPUT);
    f->st = FLT_ST_SAMPLE;
    f->hit = 0;
}

static void fall_on_confirm(void)
{
    rt_kprintf("[KEY] FALL confirmed\n");
    mode_post_event(MODE_EVT_FALL);
}

static void fall_sample_poll(key_filter_t *f)
{
    int active;

    if (f->st == FLT_ST_IDLE)
    {
        return;
    }

    active = flt_is_active(f);

    if (f->st == FLT_ST_WAIT_REL)
    {
        if (!active)
        {
            flt_rearm(f);
        }
        return;
    }

    if (!active)
    {
        flt_rearm(f);
        return;
    }

    if (f->hit < 0xFF)
    {
        f->hit++;
    }
    if (f->hit >= KEY_FILTER_CNT)
    {
        fall_on_confirm();
        f->st = FLT_ST_WAIT_REL;
    }
}

static void usb_rearm(void)
{
    rt_pin_mode(USB_IN_RT_PIN, PIN_MODE_INPUT);
    rt_pin_irq_enable(USB_IN_RT_PIN, PIN_IRQ_ENABLE);
    s_usb_flt.st = FLT_ST_IDLE;
    s_usb_flt.hit = 0;
    s_usb_flt.target_lvl = -1;
}

static void usb_enter(void)
{
    rt_pin_mode(USB_IN_RT_PIN, PIN_MODE_INPUT);
    s_usb_flt.st = FLT_ST_SAMPLE;
    s_usb_flt.hit = 0;
    s_usb_flt.target_lvl = -1;
}

/**
 * 双沿滤波：连续 KEY_FILTER_CNT 次同一电平则确认。
 * 低=插入 → USB_IN；高=拔出 → USB_OUT。与上次相同则不上报。
 */
static void usb_sample_poll(void)
{
    int lvl;

    if (s_usb_flt.st != FLT_ST_SAMPLE)
    {
        return;
    }

    lvl = rt_pin_read(USB_IN_RT_PIN);

    if (s_usb_flt.target_lvl < 0)
    {
        s_usb_flt.target_lvl = lvl;
        s_usb_flt.hit = 1;
        return;
    }

    if (lvl != s_usb_flt.target_lvl)
    {
        usb_rearm();
        return;
    }

    if (s_usb_flt.hit < 0xFF)
    {
        s_usb_flt.hit++;
    }

    if (s_usb_flt.hit < KEY_FILTER_CNT)
    {
        return;
    }

    if (s_usb_flt.target_lvl != s_usb_flt.last_lvl)
    {
        if (s_usb_flt.target_lvl == PIN_LOW)
        {
            rt_kprintf("[KEY] USB_IN\n");
            mode_post_event(MODE_EVT_USB_IN);
        }
        else
        {
            rt_kprintf("[KEY] USB_OUT\n");
            mode_post_event(MODE_EVT_USB_OUT);
        }
        s_usb_flt.last_lvl = s_usb_flt.target_lvl;
    }
    usb_rearm();
}

static void sim_rearm(void)
{
    rt_pin_mode(RD_BD_SIMCARD_RT_PIN, PIN_MODE_INPUT);
    rt_pin_irq_enable(RD_BD_SIMCARD_RT_PIN, PIN_IRQ_ENABLE);
    s_sim_flt.st = FLT_ST_IDLE;
    s_sim_flt.hit = 0;
    s_sim_flt.target_lvl = -1;
}

static void sim_enter(void)
{
    rt_pin_mode(RD_BD_SIMCARD_RT_PIN, PIN_MODE_INPUT);
    s_sim_flt.st = FLT_ST_SAMPLE;
    s_sim_flt.hit = 0;
    s_sim_flt.target_lvl = -1;
}

/**
 * SIM 双沿滤波：连续 KEY_FILTER_CNT 次同一电平确认。
 * 低=有卡；高=无卡。仅更新 sim_present + 日志。
 */
static void sim_sample_poll(void)
{
    int lvl;

    if (s_sim_flt.st != FLT_ST_SAMPLE)
    {
        return;
    }

    lvl = rt_pin_read(RD_BD_SIMCARD_RT_PIN);

    if (s_sim_flt.target_lvl < 0)
    {
        s_sim_flt.target_lvl = lvl;
        s_sim_flt.hit = 1;
        return;
    }

    if (lvl != s_sim_flt.target_lvl)
    {
        sim_rearm();
        return;
    }

    if (s_sim_flt.hit < 0xFF)
    {
        s_sim_flt.hit++;
    }

    if (s_sim_flt.hit < KEY_FILTER_CNT)
    {
        return;
    }

    if (s_sim_flt.target_lvl != s_sim_flt.last_lvl)
    {
        if (s_sim_flt.target_lvl == PIN_LOW)
        {
            s_sim_present = 1;
            rt_kprintf("[KEY] SIM_IN (card present)\n");
        }
        else
        {
            s_sim_present = 0;
            rt_kprintf("[KEY] SIM_OUT (no card)\n");
        }
        s_sim_flt.last_lvl = s_sim_flt.target_lvl;
    }
    sim_rearm();
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
        rt_pin_mode(SOS_KEY_RT_PIN, PIN_MODE_INPUT);
        s_sos_st = SOS_ST_SAMPLE;
        s_sos_hit = 0;
        s_sos_long_done = 0;
    }
    if (pending & KEY_EVT_USB_IN)
    {
        usb_enter();
    }
    if (pending & KEY_EVT_FALL)
    {
        flt_enter(&s_fall_flt);
    }
    if (pending & KEY_EVT_SIM)
    {
        sim_enter();
    }
}

static void sos_sample_poll(void)
{
    int active;

    if (s_sos_st == SOS_ST_IDLE)
    {
        return;
    }

    active = sos_is_active();

    if (s_sos_st == SOS_ST_WAIT_REL)
    {
        if (!active)
        {
            sos_rearm();
        }
        return;
    }

    if (!active)
    {
        if (!s_sos_long_done &&
            (s_sos_hit >= SOS_SHORT_CNT) &&
            (s_sos_hit < SOS_LONG_CNT))
        {
            sos_on_short();
        }
        sos_rearm();
        return;
    }

    if (s_sos_hit < 0xFF)
    {
        s_sos_hit++;
    }
    if (!s_sos_long_done && (s_sos_hit >= SOS_LONG_CNT))
    {
        s_sos_long_done = 1;
        sos_on_long();
        s_sos_st = SOS_ST_WAIT_REL;
    }
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
        fall_sample_poll(&s_fall_flt);
    }
}

int key_init(void)
{
    rt_err_t err;

    s_sos_st = SOS_ST_IDLE;
    s_sos_hit = 0;
    s_sos_long_done = 0;

    s_usb_flt.st = FLT_ST_IDLE;
    s_usb_flt.hit = 0;
    s_usb_flt.target_lvl = -1;
    s_usb_flt.last_lvl = -1;

    s_sim_flt.st = FLT_ST_IDLE;
    s_sim_flt.hit = 0;
    s_sim_flt.target_lvl = -1;
    s_sim_flt.last_lvl = -1;

    s_fall_flt.st = FLT_ST_IDLE;
    s_fall_flt.hit = 0;
    s_fall_flt.pin = FALL_KEY_RT_PIN;
    s_fall_flt.active_low = 0;
    s_fall_flt.name = "FALL";

#if USE_KEY_IRQ
    if (board_key_irq_init(key_isr_hook, RT_NULL) != 0)
    {
        rt_kprintf("[KEY] irq init failed\n");
        return -1;
    }
#endif

    /* 与 mode 上电采样对齐，避免重复 USB_IN */
    rt_pin_mode(USB_IN_RT_PIN, PIN_MODE_INPUT);
    s_usb_flt.last_lvl = rt_pin_read(USB_IN_RT_PIN);

    /* SIM 初始电平：低=有卡 */
    rt_pin_mode(RD_BD_SIMCARD_RT_PIN, PIN_MODE_INPUT);
    s_sim_flt.last_lvl = rt_pin_read(RD_BD_SIMCARD_RT_PIN);
    s_sim_present = (s_sim_flt.last_lvl == PIN_LOW) ? 1 : 0;
    if (!s_sim_present)
    {
        rt_kprintf("[KEY] SIM no card at boot\n");
    }

    rt_sem_init(&s_key_sem, "key", 0, RT_IPC_FLAG_FIFO);

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
    rt_kprintf("[KEY] ready poll=%dms filter=%u USB/SIM dual-edge sim=%d\n",
               KEY_POLL_MS, (unsigned)KEY_FILTER_CNT, s_sim_present);
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
    return 1; /* 无 KEY 模块时不挡会话 */
}

#endif /* USE_KEY */
