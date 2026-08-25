/**
 * @file pwr_plna.c
 * @brief EN_PLNA 引用计数：多模块共享，禁止单方强行关断
 */
#include "pwr_plna.h"
#include "board_pins.h"
#include "n32wb452_gpio.h"
#include "n32wb452_rcc.h"

#include <rtthread.h>

static struct rt_mutex s_lock;
static uint8_t s_ref;
static uint8_t s_inited;

void pwr_plna_init(void)
{
    GPIO_InitType gpio;

    if (s_inited)
    {
        return;
    }

    RCC_EnableAPB2PeriphClk(EN_PLNA_POW_CLK, ENABLE);
    GPIO_InitStruct(&gpio);
    gpio.Pin        = EN_PLNA_POW_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(EN_PLNA_POW_PORT, &gpio);
    PIN_RESET(EN_PLNA_POW_PORT, EN_PLNA_POW_PIN);

    rt_mutex_init(&s_lock, "plna", RT_IPC_FLAG_PRIO);
    s_ref = 0;
    s_inited = 1;
}

void pwr_plna_acquire(void)
{
    if (!s_inited)
    {
        pwr_plna_init();
    }

    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    if (s_ref == 0)
    {
        PIN_SET(EN_PLNA_POW_PORT, EN_PLNA_POW_PIN);
    }
    if (s_ref < 255)
    {
        s_ref++;
    }
    rt_mutex_release(&s_lock);
}

void pwr_plna_release(void)
{
    if (!s_inited)
    {
        return;
    }

    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    if (s_ref > 0)
    {
        s_ref--;
        if (s_ref == 0)
        {
            PIN_RESET(EN_PLNA_POW_PORT, EN_PLNA_POW_PIN);
        }
    }
    rt_mutex_release(&s_lock);
}

uint8_t pwr_plna_refcount(void)
{
    return s_ref;
}

void pwr_plna_force_off(void)
{
    if (!s_inited)
    {
        PIN_WRITE(EN_PLNA_POW_PORT, EN_PLNA_POW_PIN, EN_PLNA_POW_OFF_LEVEL);
        return;
    }

    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    s_ref = 0;
    PIN_WRITE(EN_PLNA_POW_PORT, EN_PLNA_POW_PIN, EN_PLNA_POW_OFF_LEVEL);
    rt_mutex_release(&s_lock);
}
