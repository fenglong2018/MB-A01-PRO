/**
 * @file cfg.c
 * @brief 身份/配置 RAM + Flash。不写 MODE。$BDICP 走 cfg_note_bd_card。
 */
#include "cfg.h"
#include "nvflash.h"
#include <stdio.h>
#include <string.h>
#include <rtthread.h>

#define CFG_NV_EVT_SAVE     (1u << 0)
#ifndef CFG_NV_THREAD_STACK
#define CFG_NV_THREAD_STACK 1024
#endif
#ifndef CFG_NV_THREAD_PRIO
#define CFG_NV_THREAD_PRIO  22
#endif

typedef struct __attribute__((packed))
{
    uint32_t recv_id;
    uint32_t device_id;
    uint16_t charge_offset_mv;
    uint16_t reserved;
    uint32_t bd_card[CFG_BD_SLOTS];
    char     iccid[CFG_ICCID_SLOTS][CFG_ICCID_LEN];
    char     hw_ver[CFG_VER_LEN];
    char     sw_ver[CFG_VER_LEN];
    uint32_t upgrade_unix;
    uint32_t first_fix_unix;
} cfg_nv_t;

static uint32_t s_recv_id = CFG_DEFAULT_RECV_ID;
static uint32_t s_device_id = CFG_DEFAULT_DEVICE_ID;
static uint8_t  s_pa_enable;
static uint16_t s_charge_offset_mv = CFG_DEFAULT_CHARGE_OFFSET_MV;
static uint32_t s_bd_card[CFG_BD_SLOTS];
static uint32_t s_first_fix_unix;
static cfg_nv_t s_nv;
static uint8_t  s_dirty;

static struct rt_thread s_nv_thread;
static rt_uint8_t s_nv_stack[CFG_NV_THREAD_STACK];
static struct rt_event s_nv_ev;
static uint8_t s_nv_ready;

static void nv_defaults(cfg_nv_t *n)
{
    memset(n, 0, sizeof(*n));
    n->recv_id = CFG_DEFAULT_RECV_ID;
    n->device_id = CFG_DEFAULT_DEVICE_ID;
    n->charge_offset_mv = CFG_DEFAULT_CHARGE_OFFSET_MV;
    strncpy(n->sw_ver, CFG_SW_VER, CFG_VER_LEN - 1u);
    strncpy(n->hw_ver, CFG_HW_VER, CFG_VER_LEN - 1u);
}

static void nv_to_ram(const cfg_nv_t *n)
{
    memcpy(&s_nv, n, sizeof(s_nv));
    if (n->recv_id != 0)
    {
        s_recv_id = n->recv_id;
    }
    if (n->device_id != 0)
    {
        s_device_id = n->device_id;
    }
    s_charge_offset_mv = n->charge_offset_mv;
    if (s_charge_offset_mv > CFG_CHARGE_OFFSET_MV_MAX)
    {
        s_charge_offset_mv = CFG_CHARGE_OFFSET_MV_MAX;
        s_nv.charge_offset_mv = s_charge_offset_mv;
    }
    memcpy(s_bd_card, n->bd_card, sizeof(s_bd_card));
    s_first_fix_unix = n->first_fix_unix;
    s_nv.hw_ver[CFG_VER_LEN - 1u] = '\0';
    s_nv.sw_ver[CFG_VER_LEN - 1u] = '\0';
}

static void kick_save(void)
{
    s_dirty = 1;
    if (s_nv_ready)
    {
        rt_event_send(&s_nv_ev, CFG_NV_EVT_SAVE);
    }
}

static void nv_thread_entry(void *p)
{
    rt_uint32_t set;

    (void)p;
    while (1)
    {
        set = 0;
        if (rt_event_recv(&s_nv_ev, CFG_NV_EVT_SAVE,
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          RT_WAITING_FOREVER, &set) != RT_EOK)
        {
            continue;
        }
        if (!s_dirty)
        {
            continue;
        }
        if (nvflash_save(&s_nv, (uint16_t)sizeof(s_nv)) == 0)
        {
            s_dirty = 0;
        }
        else
        {
            rt_kprintf("[CFG] nvflash save fail\n");
        }
    }
}

int cfg_init(void)
{
    cfg_nv_t loaded;

    nv_defaults(&s_nv);
    s_recv_id = CFG_DEFAULT_RECV_ID;
    s_device_id = CFG_DEFAULT_DEVICE_ID;
    s_pa_enable = 0;
    s_charge_offset_mv = CFG_DEFAULT_CHARGE_OFFSET_MV;
    memset(s_bd_card, 0, sizeof(s_bd_card));
    s_first_fix_unix = 0;
    s_dirty = 0;

    (void)nvflash_init();
    if (nvflash_load(&loaded, (uint16_t)sizeof(loaded)) == (int)sizeof(loaded))
    {
        nv_to_ram(&loaded);
        rt_kprintf("[CFG] flash load ok id=%lu recv=%lu\n",
                   (unsigned long)s_device_id, (unsigned long)s_recv_id);
    }
    else
    {
        rt_kprintf("[CFG] flash empty, defaults\n");
    }
    /* 软件版本跟当前固件宏，与 Flash 里旧字符串不一致则更新 */
    if (strncmp(s_nv.sw_ver, CFG_SW_VER, CFG_VER_LEN) != 0)
    {
        memset(s_nv.sw_ver, 0, sizeof(s_nv.sw_ver));
        strncpy(s_nv.sw_ver, CFG_SW_VER, CFG_VER_LEN - 1u);
        kick_save();
    }
    return 0;
}

uint32_t cfg_get_recv_id(void)
{
    return s_recv_id;
}

void cfg_set_recv_id(uint32_t id)
{
    if (id == 0)
    {
        return;
    }
    s_recv_id = id;
    if (s_nv.recv_id != id)
    {
        s_nv.recv_id = id;
        kick_save();
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
    if (id == 0)
    {
        return;
    }
    s_device_id = id;
    if (s_nv.device_id != id)
    {
        s_nv.device_id = id;
        kick_save();
    }
}

uint16_t cfg_get_charge_offset_mv(void)
{
    return s_charge_offset_mv;
}

void cfg_set_charge_offset_mv(uint16_t mv)
{
    if (mv > CFG_CHARGE_OFFSET_MV_MAX)
    {
        mv = CFG_CHARGE_OFFSET_MV_MAX;
    }
    s_charge_offset_mv = mv;
    if (s_nv.charge_offset_mv != mv)
    {
        s_nv.charge_offset_mv = mv;
        kick_save();
    }
}

void cfg_note_bd_card(uint32_t id)
{
    uint8_t i;

    if (id == 0)
    {
        return;
    }
    if (s_bd_card[0] == id)
    {
        return;
    }
    for (i = CFG_BD_SLOTS - 1u; i > 0; i--)
    {
        s_bd_card[i] = s_bd_card[i - 1u];
        s_nv.bd_card[i] = s_nv.bd_card[i - 1u];
    }
    s_bd_card[0] = id;
    s_nv.bd_card[0] = id;
    kick_save();
}

uint32_t cfg_get_bd_card(void)
{
    return s_bd_card[0];
}

uint32_t cfg_get_first_fix_unix(void)
{
    return s_first_fix_unix;
}

void cfg_set_first_fix_unix(uint32_t unix_sec)
{
    if ((unix_sec == 0) || (s_first_fix_unix != 0))
    {
        return;
    }
    s_first_fix_unix = unix_sec;
    s_nv.first_fix_unix = unix_sec;
    kick_save();
}

const char *cfg_get_hw_ver(void)
{
    return s_nv.hw_ver;
}

const char *cfg_get_sw_ver(void)
{
    return s_nv.sw_ver;
}

uint32_t cfg_get_upgrade_unix(void)
{
    return s_nv.upgrade_unix;
}

void cfg_set_hw_ver(const char *ver)
{
    char tmp[CFG_VER_LEN];
    uint32_t i;

    if ((ver == 0) || (ver[0] == '\0'))
    {
        return;
    }
    memset(tmp, 0, sizeof(tmp));
    for (i = 0; (i < (CFG_VER_LEN - 1u)) && (ver[i] != '\0'); i++)
    {
        char c = ver[i];
        if ((c == '"') || (c == '\\') || (c == '\n') || (c == '\r'))
        {
            return;
        }
        tmp[i] = c;
    }
    if (strncmp(s_nv.hw_ver, tmp, CFG_VER_LEN) == 0)
    {
        return;
    }
    memcpy(s_nv.hw_ver, tmp, CFG_VER_LEN);
    kick_save();
}

int cfg_to_json(char *buf, int buflen)
{
    if ((buf == 0) || (buflen < 80))
    {
        return -1;
    }
    return snprintf(buf, (size_t)buflen,
                    "\"recv_id\":%lu,\"pa_enable\":%u,\"device_id\":%lu,"
                    "\"charge_offset_mv\":%u,\"bd_card\":%lu,\"first_fix_unix\":%lu,"
                    "\"hw_ver\":\"%s\",\"sw_ver\":\"%s\",\"upgrade_unix\":%lu",
                    (unsigned long)s_recv_id,
                    (unsigned)s_pa_enable,
                    (unsigned long)s_device_id,
                    (unsigned)s_charge_offset_mv,
                    (unsigned long)s_bd_card[0],
                    (unsigned long)s_first_fix_unix,
                    s_nv.hw_ver,
                    s_nv.sw_ver,
                    (unsigned long)s_nv.upgrade_unix);
}

static int app_cfg_init(void)
{
    return cfg_init();
}

static int app_cfg_nv_thread(void)
{
    rt_err_t err;

    rt_event_init(&s_nv_ev, "cfgnv", RT_IPC_FLAG_FIFO);
    err = rt_thread_init(&s_nv_thread, "cfgnv", nv_thread_entry, RT_NULL,
                         s_nv_stack, sizeof(s_nv_stack), CFG_NV_THREAD_PRIO, 20);
    if (err != RT_EOK)
    {
        rt_kprintf("[CFG] nv thread fail\n");
        return -1;
    }
    rt_thread_startup(&s_nv_thread);
    s_nv_ready = 1;
    if (s_dirty)
    {
        rt_event_send(&s_nv_ev, CFG_NV_EVT_SAVE);
    }
    return 0;
}

INIT_ENV_EXPORT(app_cfg_init);
INIT_APP_EXPORT(app_cfg_nv_thread);
