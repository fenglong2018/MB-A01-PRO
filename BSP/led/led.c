/**
 * @file led.c
 * @brief 电量 LED：OFF/FORCE_OFF 灭；ON/ALARM 三灯同闪；
 *        BATT 流水一轮；CHARGE 流水循环，≥98% 三灯常亮。
 */
#include <rtthread.h>
#include "config.h"
#include "led.h"
#include "board_pins.h"
#include "mode.h"
#include "n32wb452_gpio.h"
#include "n32wb452_rcc.h"

#if USE_LED

#ifndef LED_MQ_DEPTH
#define LED_MQ_DEPTH            8
#endif

typedef enum
{
    ANIM_IDLE = 0,
    ANIM_WAIT_PULSE,    /* ON/ALARM 等待下次闪 */
    ANIM_PULSE_ON,
    ANIM_FLOW_1,
    ANIM_FLOW_2,
    ANIM_FLOW_3,
    ANIM_FLOW_GAP,      /* CHARGE 轮间；BATT 一轮后进 IDLE */
    ANIM_FULL_ON,
} led_anim_t;

static struct rt_thread s_led_thread;
static rt_uint8_t s_led_stack[LED_THREAD_STACK];
static struct rt_messagequeue s_led_mq;
static rt_uint8_t s_led_mq_pool[LED_MQ_DEPTH * sizeof(led_msg_t)];

static uint8_t s_percent;
static mode_state_t s_mode;
static led_anim_t s_anim;
static rt_tick_t s_deadline;
static uint8_t s_batt_flow_done; /* BATT 已播完一轮 */

static void led_hw_init(void)
{
    GPIO_InitType gpio;

    RCC_EnableAPB2PeriphClk(LED1_CLK | LED2_CLK | LED3_CLK, ENABLE);

    GPIO_InitStruct(&gpio);
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    gpio.Pin = LED1_PIN;
    GPIO_InitPeripheral(LED1_PORT, &gpio);
    gpio.Pin = LED2_PIN;
    GPIO_InitPeripheral(LED2_PORT, &gpio);
    gpio.Pin = LED3_PIN;
    GPIO_InitPeripheral(LED3_PORT, &gpio);
}

static void led_write(int l1, int l2, int l3)
{
    /* 逻辑 1=亮 / 0=灭；硬件低电平点亮，写脚时取反 */
    PIN_WRITE(LED1_PORT, LED1_PIN, l1 ? 0 : 1);
    PIN_WRITE(LED2_PORT, LED2_PIN, l2 ? 0 : 1);
    PIN_WRITE(LED3_PORT, LED3_PIN, l3 ? 0 : 1);
}

static void led_all_off(void)
{
    led_write(0, 0, 0);
}

static void led_all_on(void)
{
    led_write(1, 1, 1);
}

/** 流水档位：1=仅LED1，2=LED1+2，3=三灯 */
static uint8_t flow_steps(uint8_t pct)
{
    if (pct < LED_BAND_LOW_PCT)
    {
        return 1;
    }
    if (pct < LED_BAND_HIGH_PCT)
    {
        return 2;
    }
    return 3;
}

static void flow_show_step(uint8_t step)
{
    switch (step)
    {
    case 1:
        led_write(1, 0, 0);
        break;
    case 2:
        led_write(1, 1, 0);
        break;
    case 3:
        led_write(1, 1, 1);
        break;
    default:
        led_all_off();
        break;
    }
}

static void anim_enter_for_mode(void)
{
    s_batt_flow_done = 0;
    led_all_off();

    switch (s_mode)
    {
    case MODE_ST_OFF:
    case MODE_ST_FORCE_OFF:
        s_anim = ANIM_IDLE;
        s_deadline = 0;
        break;

    case MODE_ST_ON:
    case MODE_ST_LOW_BATT:
        /* 进态先闪一下，再按 10s 周期；LOW_BATT 同 ON 显示 */
        led_all_on();
        s_anim = ANIM_PULSE_ON;
        s_deadline = rt_tick_get() + rt_tick_from_millisecond(LED_ON_PULSE_MS);
        break;

    case MODE_ST_ALARM:
        led_all_on();
        s_anim = ANIM_PULSE_ON;
        s_deadline = rt_tick_get() + rt_tick_from_millisecond(LED_ALARM_PULSE_MS);
        break;

    case MODE_ST_BATT:
        s_anim = ANIM_FLOW_1;
        flow_show_step(1);
        s_deadline = rt_tick_get() + rt_tick_from_millisecond(LED_FLOW_STEP_MS);
        break;

    case MODE_ST_CHARGE:
        if (s_percent >= LED_CHARGE_FULL_PCT)
        {
            s_anim = ANIM_FULL_ON;
            led_all_on();
            s_deadline = 0;
        }
        else
        {
            s_anim = ANIM_FLOW_1;
            flow_show_step(1);
            s_deadline = rt_tick_get() + rt_tick_from_millisecond(LED_FLOW_STEP_MS);
        }
        break;

    default:
        s_anim = ANIM_IDLE;
        s_deadline = 0;
        break;
    }
}

static void anim_on_tick(void)
{
    uint8_t steps = flow_steps(s_percent);

    switch (s_anim)
    {
    case ANIM_WAIT_PULSE:
        led_all_on();
        s_anim = ANIM_PULSE_ON;
        if (s_mode == MODE_ST_ALARM)
        {
            s_deadline = rt_tick_get() + rt_tick_from_millisecond(LED_ALARM_PULSE_MS);
        }
        else
        {
            s_deadline = rt_tick_get() + rt_tick_from_millisecond(LED_ON_PULSE_MS);
        }
        break;

    case ANIM_PULSE_ON:
        led_all_off();
        s_anim = ANIM_WAIT_PULSE;
        if (s_mode == MODE_ST_ALARM)
        {
            s_deadline = rt_tick_get() +
                         rt_tick_from_millisecond(LED_ALARM_PERIOD_MS - LED_ALARM_PULSE_MS);
        }
        else
        {
            s_deadline = rt_tick_get() +
                         rt_tick_from_millisecond(LED_ON_PERIOD_MS - LED_ON_PULSE_MS);
        }
        break;

    case ANIM_FLOW_1:
        if (steps >= 2)
        {
            s_anim = ANIM_FLOW_2;
            flow_show_step(2);
            s_deadline = rt_tick_get() + rt_tick_from_millisecond(LED_FLOW_STEP_MS);
        }
        else
        {
            led_all_off();
            if (s_mode == MODE_ST_BATT)
            {
                s_batt_flow_done = 1;
                s_anim = ANIM_IDLE;
                s_deadline = 0;
            }
            else
            {
                s_anim = ANIM_FLOW_GAP;
                s_deadline = rt_tick_get() + rt_tick_from_millisecond(LED_FLOW_GAP_MS);
            }
        }
        break;

    case ANIM_FLOW_2:
        if (steps >= 3)
        {
            s_anim = ANIM_FLOW_3;
            flow_show_step(3);
            s_deadline = rt_tick_get() + rt_tick_from_millisecond(LED_FLOW_STEP_MS);
        }
        else
        {
            led_all_off();
            if (s_mode == MODE_ST_BATT)
            {
                s_batt_flow_done = 1;
                s_anim = ANIM_IDLE;
                s_deadline = 0;
            }
            else
            {
                s_anim = ANIM_FLOW_GAP;
                s_deadline = rt_tick_get() + rt_tick_from_millisecond(LED_FLOW_GAP_MS);
            }
        }
        break;

    case ANIM_FLOW_3:
        led_all_off();
        if (s_mode == MODE_ST_BATT)
        {
            s_batt_flow_done = 1;
            s_anim = ANIM_IDLE;
            s_deadline = 0;
        }
        else
        {
            s_anim = ANIM_FLOW_GAP;
            s_deadline = rt_tick_get() + rt_tick_from_millisecond(LED_FLOW_GAP_MS);
        }
        break;

    case ANIM_FLOW_GAP:
        /* 仅 CHARGE 循环 */
        if ((s_mode == MODE_ST_CHARGE) && (s_percent >= LED_CHARGE_FULL_PCT))
        {
            s_anim = ANIM_FULL_ON;
            led_all_on();
            s_deadline = 0;
        }
        else if (s_mode == MODE_ST_CHARGE)
        {
            s_anim = ANIM_FLOW_1;
            flow_show_step(1);
            s_deadline = rt_tick_get() + rt_tick_from_millisecond(LED_FLOW_STEP_MS);
        }
        else
        {
            s_anim = ANIM_IDLE;
            s_deadline = 0;
        }
        break;

    case ANIM_FULL_ON:
    case ANIM_IDLE:
    default:
        break;
    }
}

static void on_percent(uint8_t pct)
{
    uint8_t old = s_percent;

    if (pct > 100)
    {
        pct = 100;
    }
    s_percent = pct;

    if (s_mode != MODE_ST_CHARGE)
    {
        return;
    }

    /* 充电极电 / 离开满电：切换显示 */
    if ((old < LED_CHARGE_FULL_PCT) && (pct >= LED_CHARGE_FULL_PCT))
    {
        s_anim = ANIM_FULL_ON;
        led_all_on();
        s_deadline = 0;
    }
    else if ((old >= LED_CHARGE_FULL_PCT) && (pct < LED_CHARGE_FULL_PCT))
    {
        s_anim = ANIM_FLOW_1;
        flow_show_step(1);
        s_deadline = rt_tick_get() + rt_tick_from_millisecond(LED_FLOW_STEP_MS);
    }
}

static void on_mode(uint8_t mode)
{
    s_mode = (mode_state_t)mode;
    anim_enter_for_mode();
}

static void led_thread_entry(void *param)
{
    led_msg_t msg;
    rt_int32_t wait;

    (void)param;

    while (1)
    {
        if ((s_deadline != 0) &&
            ((s_anim == ANIM_WAIT_PULSE) || (s_anim == ANIM_PULSE_ON) ||
             (s_anim == ANIM_FLOW_1) || (s_anim == ANIM_FLOW_2) ||
             (s_anim == ANIM_FLOW_3) || (s_anim == ANIM_FLOW_GAP)))
        {
            rt_tick_t now = rt_tick_get();
            rt_int32_t left = (rt_int32_t)(s_deadline - now);
            wait = (left > 0) ? left : 0;
        }
        else
        {
            wait = RT_WAITING_FOREVER;
        }

        if (rt_mq_recv(&s_led_mq, &msg, sizeof(msg), wait) == RT_EOK)
        {
            if (msg.kind == LED_MSG_PERCENT)
            {
                on_percent(msg.value);
            }
            else if (msg.kind == LED_MSG_MODE)
            {
                on_mode(msg.value);
            }
        }

        if ((s_deadline != 0) &&
            ((rt_int32_t)(s_deadline - rt_tick_get()) <= 0))
        {
            anim_on_tick();
        }
    }
}

static void led_post(uint8_t kind, uint8_t value)
{
    led_msg_t msg;

    msg.kind  = kind;
    msg.value = value;
    if (rt_mq_send(&s_led_mq, &msg, sizeof(msg)) != RT_EOK)
    {
        /* 满则丢掉最旧再发 */
        led_msg_t dump;
        rt_mq_recv(&s_led_mq, &dump, sizeof(dump), 0);
        rt_mq_send(&s_led_mq, &msg, sizeof(msg));
    }
}

void led_post_percent(uint8_t percent)
{
    led_post(LED_MSG_PERCENT, percent);
}

void led_post_mode(uint8_t mode)
{
    led_post(LED_MSG_MODE, mode);
}

int led_init(void)
{
    rt_err_t err;

    s_percent = 0;
    s_mode = MODE_ST_OFF;
    s_anim = ANIM_IDLE;
    s_deadline = 0;
    s_batt_flow_done = 0;

    led_hw_init();
    led_all_off();

    rt_mq_init(&s_led_mq,
               "ledmq",
               s_led_mq_pool,
               sizeof(led_msg_t),
               sizeof(s_led_mq_pool),
               RT_IPC_FLAG_FIFO);

    err = rt_thread_init(&s_led_thread,
                         "led",
                         led_thread_entry,
                         RT_NULL,
                         s_led_stack,
                         sizeof(s_led_stack),
                         LED_THREAD_PRIO,
                         10);
    if (err != RT_EOK)
    {
        rt_kprintf("[LED] thread init failed\n");
        return -1;
    }
    rt_thread_startup(&s_led_thread);
    rt_kprintf("[LED] ready\n");
    return 0;
}

static int app_led_init(void)
{
    return led_init();
}
INIT_APP_EXPORT(app_led_init);

#endif /* USE_LED */
