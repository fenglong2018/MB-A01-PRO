/**
 * @file cli_io.c
 * @brief 可调试 IO 白名单骨架（列出/读/写）
 *
 * 骨架阶段：表已就绪，真正 GPIO 操作标为 not_ready，避免误拨未初始化脚。
 * 后续在对应项挂上 init + set/get 即可。
 */
#include "cli_io.h"
#include "board_pins.h"

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
    uint8_t ready; /* 1=已接 GPIO 实现 */
} cli_io_item_t;

static const cli_io_item_t s_io_table[] = {
    {"EN_5V", CLI_IO_DIR_OUT, 0},
    {"EN_PLNA_POW", CLI_IO_DIR_OUT, 0},
    {"EN_LNA_GNSS", CLI_IO_DIR_OUT, 0},
    {"EN_PGNSS", CLI_IO_DIR_OUT, 0},
    {"EN_LNA_RDSS", CLI_IO_DIR_OUT, 0},
    {"EN_PRDSS", CLI_IO_DIR_OUT, 0},
    {"EN_BLE", CLI_IO_DIR_OUT, 0},
    {"LED1", CLI_IO_DIR_OUT, 0},
    {"LED2", CLI_IO_DIR_OUT, 0},
    {"LED3", CLI_IO_DIR_OUT, 0},
    {"USB_IN", CLI_IO_DIR_IN, 0},
    {"SOS_KEY", CLI_IO_DIR_IN, 0},
    {"FALL_KEY", CLI_IO_DIR_IN, 0},
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
                       "%s{\"name\":\"%s\",\"dir\":\"%s\",\"ready\":%u}",
                       (i == 0) ? "" : ",",
                       s_io_table[i].name,
                       (s_io_table[i].dir == CLI_IO_DIR_OUT) ? "out" : "in",
                       (unsigned)s_io_table[i].ready);
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

    (void)EN_5V_PA_POW_PORT; /* 保持与 board_pins.h 关联，便于后续接线 */
    return n;
}

int cli_io_get(const char *pin, int *val)
{
    const cli_io_item_t *item = cli_io_find(pin);

    if ((item == NULL) || (val == NULL))
    {
        return -1;
    }
    if (!item->ready)
    {
        return -2; /* not_ready */
    }
    return -2;
}

int cli_io_set(const char *pin, int val)
{
    const cli_io_item_t *item = cli_io_find(pin);

    (void)val;
    if (item == NULL)
    {
        return -1;
    }
    if (item->dir != CLI_IO_DIR_OUT)
    {
        return -3; /* readonly */
    }
    if (!item->ready)
    {
        return -2;
    }
    return -2;
}
