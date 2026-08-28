/**
 * @file cli_io.c
 * @brief 调试 IO 白名单：io.list / io.get / io.set 读写真实 GPIO
 *
 * val 是脚上电平 0/1，不是“有效”。低有效脚由上位机勾选时取反。
 * 电源轨会和 GNSS/RDSS 会话抢脚；CHARGE 下 LED 动画会覆盖 LED 写入。
 */
#include "cli_io.h"
#include "board_pins.h"
#include "n32wb452_gpio.h"

#include <stdio.h>
#include <string.h>

typedef enum
{
    CLI_IO_DIR_IN = 0,
    CLI_IO_DIR_OUT = 1
} cli_io_dir_t;

typedef struct
{
    const char *name;
    cli_io_dir_t dir;
    GPIO_Module *port;
    uint16_t pin;
} cli_io_item_t;

static const cli_io_item_t s_io_table[] = {
    {"EN_5V",       CLI_IO_DIR_OUT, EN_5V_PA_POW_PORT,    EN_5V_PA_POW_PIN},
    {"EN_PLNA_POW", CLI_IO_DIR_OUT, EN_PLNA_POW_PORT,     EN_PLNA_POW_PIN},
    {"EN_LNA_GNSS", CLI_IO_DIR_OUT, EN_LNA_POW_GNSS_PORT, EN_LNA_POW_GNSS_PIN},
    {"EN_PGNSS",    CLI_IO_DIR_OUT, EN_PGNSS_POW_PORT,    EN_PGNSS_POW_PIN},
    {"EN_LNA_RDSS", CLI_IO_DIR_OUT, EN_LNA_RDSS_POW_PORT, EN_LNA_RDSS_POW_PIN},
    {"EN_PRDSS",    CLI_IO_DIR_OUT, EN_PRDSS_POW_PORT,    EN_PRDSS_POW_PIN},
    {"EN_BLE",      CLI_IO_DIR_OUT, EN_BLE_POW_PORT,      EN_BLE_POW_PIN},
    {"LED1",        CLI_IO_DIR_OUT, LED1_PORT,            LED1_PIN},
    {"LED2",        CLI_IO_DIR_OUT, LED2_PORT,            LED2_PIN},
    {"LED3",        CLI_IO_DIR_OUT, LED3_PORT,            LED3_PIN},
    {"USB_IN",      CLI_IO_DIR_IN,  USB_IN_PORT,          USB_IN_PIN},
    {"SOS_KEY",     CLI_IO_DIR_IN,  SOS_KEY_PORT,         SOS_KEY_PIN},
    {"FALL_KEY",    CLI_IO_DIR_IN,  FALL_KEY_PORT,        FALL_KEY_PIN},
};

static const cli_io_item_t *cli_io_find(const char *pin)
{
    int i;

    if (pin == NULL)
    {
        return NULL;
    }
    for (i = 0; i < (int)(sizeof(s_io_table) / sizeof(s_io_table[0])); i++)
    {
        if (strcmp(s_io_table[i].name, pin) == 0)
        {
            return &s_io_table[i];
        }
    }
    return NULL;
}

int cli_io_list_json(char *buf, int buflen)
{
    int i;
    int n = 0;
    int ret;

    if ((buf == NULL) || (buflen < 4))
    {
        return -1;
    }

    ret = snprintf(buf + n, (size_t)(buflen - n), "[");
    if (ret < 0 || ret >= (buflen - n))
    {
        return -1;
    }
    n += ret;

    for (i = 0; i < (int)(sizeof(s_io_table) / sizeof(s_io_table[0])); i++)
    {
        ret = snprintf(buf + n, (size_t)(buflen - n),
                       "%s{\"name\":\"%s\",\"dir\":\"%s\",\"ready\":1}",
                       (i == 0) ? "" : ",",
                       s_io_table[i].name,
                       (s_io_table[i].dir == CLI_IO_DIR_OUT) ? "out" : "in");
        if (ret < 0 || ret >= (buflen - n))
        {
            return -1;
        }
        n += ret;
    }

    ret = snprintf(buf + n, (size_t)(buflen - n), "]");
    if (ret < 0 || ret >= (buflen - n))
    {
        return -1;
    }
    n += ret;
    return n;
}

int cli_io_get(const char *pin, int *val)
{
    const cli_io_item_t *item = cli_io_find(pin);

    if ((item == NULL) || (val == NULL))
    {
        return -1;
    }
    *val = PIN_READ(item->port, item->pin) ? 1 : 0;
    return 0;
}

int cli_io_set(const char *pin, int val)
{
    const cli_io_item_t *item = cli_io_find(pin);

    if (item == NULL)
    {
        return -1;
    }
    if (item->dir != CLI_IO_DIR_OUT)
    {
        return -3;
    }
    PIN_WRITE(item->port, item->pin, val ? 1 : 0);
    return 0;
}
