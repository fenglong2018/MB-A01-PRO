/**
 * @file ble.c
 * @brief USB 脚开关 MCU_EN_BLE；Nations slave 栈 + JSON CLI 收发
 *
 * 对照官方 examples/BLE/slave：bt_ware_init / bt_run_thread / bt_handler(EXTI14)。
 * 数据面不走 NS_BlueTooth 的 2 字节长度头，nRF Connect 写一行 JSON 即可。
 */
#include <rtthread.h>
#include <rthw.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "ble.h"
#include "board_pins.h"
#include "n32wb452_gpio.h"
#include "n32wb452_rcc.h"
#include "cfg.h"
#include "cli.h"

#if USE_BLE

#include "n32wb452_ble_api.h"
#include "n32wb452_data_fifo.h"
#include "user.h"
#include "att.h"

BT_SERVER_STS gBT_STS = BT_IDLE;

static uint8_t s_on;
static uint8_t s_conn;
static uint8_t s_stack;
static uint8_t s_run;
static bt_attr_param s_bt_init;
static uint8_t s_rx_pkt[256];

static struct rt_thread s_thread;
static rt_uint8_t s_thread_stack[BLE_THREAD_STACK];
static struct rt_semaphore s_kick;
static uint8_t s_thread_ok;

static void ble_pin_off(void)
{
    PIN_WRITE(EN_BLE_POW_PORT, EN_BLE_POW_PIN, EN_BLE_POW_OFF_LEVEL);
}

static void ble_pin_on(void)
{
    PIN_WRITE(EN_BLE_POW_PORT, EN_BLE_POW_PIN, EN_BLE_POW_OFF_LEVEL ? 0 : 1);
}

void ble_name(char *buf, int bufsz)
{
    uint32_t id = 0;

    if ((buf == RT_NULL) || (bufsz < 8))
    {
        return;
    }
    id = cfg_get_device_id();
    rt_snprintf(buf, (size_t)bufsz, "%s-%04u", BLE_NAME_PREFIX,
                (unsigned)(id % 10000u));
}

static void ble_fill_addr(char *buf, int bufsz)
{
    uint32_t id = cfg_get_device_id();

    /* 本地管理位 + 单播；格式与官方 bt_init.device_addr 相同 */
    rt_snprintf(buf, (size_t)bufsz, "C2:%02X:%02X:%02X:%02X:%02X",
                (unsigned)((id >> 24) & 0xFFu),
                (unsigned)((id >> 16) & 0xFFu),
                (unsigned)((id >> 8) & 0xFFu),
                (unsigned)(id & 0xFFu),
                (unsigned)((id >> 4) & 0xFFu));
}

static void bt_event_callback_func(bt_event_enum event, const uint8_t *data,
                                   uint32_t size, uint32_t character_uuid)
{
    uint16_t length;
    static const uint8_t nl = '\n';

    (void)data;

    switch (event)
    {
    case BT_EVENT_CONNECTED:
        s_conn = 1;
        gBT_STS = BT_CONNECTED;
        rt_kprintf("[BLE] connected\n");
        break;

    case BT_EVENT_DISCONNECTD:
        s_conn = 0;
        gBT_STS = BT_DISCONNECTED;
        fifo_clear();
        rt_kprintf("[BLE] disconnect\n");
        break;

    case BT_EVENT_RCV_DATA:
        length = (uint16_t)bt_rcv_data(s_rx_pkt, size, character_uuid);
        if (length)
        {
            cli_feed(CLI_CH_BLE, s_rx_pkt, length);
            cli_feed(CLI_CH_BLE, &nl, 1u);
        }
        break;

    default:
        break;
    }
}

static void ble_thread_entry(void *param)
{
    (void)param;

    while (1)
    {
        if (!s_run)
        {
            (void)rt_sem_take(&s_kick, RT_WAITING_FOREVER);
            continue;
        }
        bt_run_thread();
        (void)rt_sem_take(&s_kick, 1);
    }
}

static int ble_thread_start(void)
{
    rt_err_t err;

    if (s_thread_ok)
    {
        return 0;
    }
    rt_sem_init(&s_kick, "ble_k", 0, RT_IPC_FLAG_FIFO);
    err = rt_thread_init(&s_thread,
                         "ble",
                         ble_thread_entry,
                         RT_NULL,
                         s_thread_stack,
                         sizeof(s_thread_stack),
                         BLE_THREAD_PRIO,
                         10);
    if (err != RT_EOK)
    {
        rt_kprintf("[BLE] thread init fail\n");
        return -1;
    }
    rt_thread_startup(&s_thread);
    s_thread_ok = 1;
    return 0;
}

static int ble_stack_init_once(void)
{
    char nm[32];
    char addr[20];
    int32_t ret;

    if (s_stack)
    {
        return 0;
    }

    memset(&s_bt_init, 0, sizeof(s_bt_init));
    ble_name(nm, (int)sizeof(nm));
    ble_fill_addr(addr, (int)sizeof(addr));
    memcpy(s_bt_init.device_name, nm,
           (strlen(nm) < sizeof(s_bt_init.device_name))
               ? strlen(nm)
               : sizeof(s_bt_init.device_name));
    memcpy(s_bt_init.device_addr, addr,
           (strlen(addr) < sizeof(s_bt_init.device_addr))
               ? strlen(addr)
               : sizeof(s_bt_init.device_addr));

    s_bt_init.service[0].svc_uuid = ATT_SVC_UKEY_SERVICE;
    s_bt_init.service[0].character[0].uuid = ATT_CHAR_WRITE_NOTIFY;
    s_bt_init.service[0].character[0].permission = BT_WRITE_PERM | BT_NTF_PERM;

    ret = bt_ware_init(&s_bt_init, (bt_event_callback_handler_t)bt_event_callback_func);
    if (ret != BT_RET_SUCCESS)
    {
        rt_kprintf("[BLE] bt_ware_init fail %d\n", (int)ret);
        return -1;
    }
    s_stack = 1;
    rt_kprintf("[BLE] stack ok name=%s\n", nm);
    return 0;
}

void n32wb452_exti14_hook(void)
{
    if (s_stack)
    {
        bt_handler();
        rt_sem_release(&s_kick);
    }
}

void ble_start(void)
{
    if (s_on)
    {
        return;
    }
    ble_pin_on();
    s_on = 1;
    s_conn = 0;
    rt_thread_mdelay(100);
    if (ble_stack_init_once() != 0)
    {
        ble_pin_off();
        s_on = 0;
        return;
    }
    if (ble_thread_start() != 0)
    {
        ble_pin_off();
        s_on = 0;
        return;
    }
    s_run = 1;
    rt_sem_release(&s_kick);
}

void ble_stop(void)
{
    if (!s_on)
    {
        return;
    }
    s_run = 0;
    if (s_conn)
    {
        bt_disconnect();
        s_conn = 0;
    }
    ble_pin_off();
    s_on = 0;
    rt_kprintf("[BLE] EN_BLE off\n");
}

int ble_is_on(void)
{
    return s_on ? 1 : 0;
}

int ble_is_connected(void)
{
    return (s_on && s_conn) ? 1 : 0;
}

int ble_stack_ready(void)
{
    return s_stack ? 1 : 0;
}

int ble_write(const uint8_t *data, uint32_t len)
{
    if (!s_on || !s_conn || !s_stack || (data == RT_NULL) || (len == 0))
    {
        return 0;
    }
    (void)bt_snd_data(data, len, USER_IDX_WRITE_NOTIFY_VAL);
    return (int)len;
}

#endif /* USE_BLE */
