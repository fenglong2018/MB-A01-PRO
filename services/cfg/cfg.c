/**
 * @file cfg.c
 * @brief 运行时配置：recv_id / pa_enable / device_id
 */
#include "cfg.h"
#include <stdio.h>
#include <string.h>
#include <rtthread.h>

static uint32_t s_recv_id = CFG_DEFAULT_RECV_ID;
static uint32_t s_device_id = CFG_DEFAULT_DEVICE_ID;
static uint8_t  s_pa_enable = 0;

int cfg_init(void)
{
    s_recv_id = CFG_DEFAULT_RECV_ID;
    s_device_id = CFG_DEFAULT_DEVICE_ID;
    s_pa_enable = 0;
    return 0;
}

uint32_t cfg_get_recv_id(void)
{
    return s_recv_id;
}

void cfg_set_recv_id(uint32_t id)
{
    if (id != 0)
    {
        s_recv_id = id;
    }
}

uint8_t cfg_get_pa_enable(void)
{
    return s_pa_enable ? 1u : 0u;
}

void cfg_set_pa_enable(uint8_t en)
{
    s_pa_enable = en ? 1u : 0u;
}

uint32_t cfg_get_device_id(void)
{
    return s_device_id;
}

void cfg_set_device_id(uint32_t id)
{
    if (id != 0)
    {
        s_device_id = id;
    }
}

int cfg_to_json(char *buf, int buflen)
{
    if ((buf == 0) || (buflen < 32))
    {
        return -1;
    }
    return snprintf(buf, (size_t)buflen,
                    "\"recv_id\":%lu,\"pa_enable\":%u,\"device_id\":%lu",
                    (unsigned long)s_recv_id,
                    (unsigned)s_pa_enable,
                    (unsigned long)s_device_id);
}

static int app_cfg_init(void)
{
    return cfg_init();
}
INIT_COMPONENT_EXPORT(app_cfg_init);
