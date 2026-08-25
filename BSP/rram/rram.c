/**
 * @file rram.c
 * @brief Retention RAM 原始块（.rram，NOLOAD）
 */
#include "rram.h"
#include <string.h>
#include <rtthread.h>

#define RRAM_MAGIC      0x52524D31u /* RRM1 */

typedef struct
{
    uint32_t magic;
    uint16_t len;
    uint16_t crc;
    uint8_t  data[RRAM_BLOB_MAX];
} rram_rec_t;

static rram_rec_t s_rram __attribute__((section(".rram"), used));

static uint16_t crc16_ccitt(const uint8_t *p, uint16_t n)
{
    uint16_t c = 0xFFFFu;
    uint16_t i;
    uint8_t b;

    for (i = 0; i < n; i++)
    {
        c ^= (uint16_t)p[i] << 8;
        for (b = 0; b < 8u; b++)
        {
            if (c & 0x8000u)
            {
                c = (uint16_t)((c << 1) ^ 0x1021u);
            }
            else
            {
                c = (uint16_t)(c << 1);
            }
        }
    }
    return c;
}

int rram_init(void)
{
    return 0;
}

int rram_save(const void *data, uint16_t len)
{
    if ((data == 0) || (len == 0) || (len > RRAM_BLOB_MAX))
    {
        return -1;
    }
    memset(&s_rram, 0, sizeof(s_rram));
    s_rram.magic = RRAM_MAGIC;
    s_rram.len = len;
    memcpy(s_rram.data, data, len);
    s_rram.crc = crc16_ccitt(s_rram.data, len);
    return 0;
}

int rram_load(void *out, uint16_t out_max)
{
    if ((out == 0) || (out_max == 0))
    {
        return -1;
    }
    if (s_rram.magic != RRAM_MAGIC)
    {
        return -1;
    }
    if ((s_rram.len == 0) || (s_rram.len > RRAM_BLOB_MAX))
    {
        return -1;
    }
    if (crc16_ccitt(s_rram.data, s_rram.len) != s_rram.crc)
    {
        return -1;
    }
    if (s_rram.len > out_max)
    {
        return -1;
    }
    memcpy(out, s_rram.data, s_rram.len);
    return (int)s_rram.len;
}

static int app_rram_init(void)
{
    return rram_init();
}
INIT_ENV_EXPORT(app_rram_init);
