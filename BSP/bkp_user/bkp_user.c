/**
 * @file bkp_user.c
 * @brief DAT3 起用户备份；DAT1/DAT2 属 RTC
 */
#include "bkp_user.h"
#include "n32wb452_bkp.h"
#include "n32wb452_pwr.h"
#include "n32wb452_rcc.h"
#include <string.h>

#define BKP_USER_MAGIC      0xB10Cu

static const uint16_t s_dat[] = {
    BKP_DAT3,  BKP_DAT4,  BKP_DAT5,  BKP_DAT6,  BKP_DAT7,
    BKP_DAT8,  BKP_DAT9,  BKP_DAT10, BKP_DAT11, BKP_DAT12,
    BKP_DAT13, BKP_DAT14, BKP_DAT15, BKP_DAT16, BKP_DAT17,
    BKP_DAT18, BKP_DAT19, BKP_DAT20, BKP_DAT21, BKP_DAT22,
    BKP_DAT23, BKP_DAT24, BKP_DAT25, BKP_DAT26, BKP_DAT27,
    BKP_DAT28, BKP_DAT29, BKP_DAT30, BKP_DAT31, BKP_DAT32,
    BKP_DAT33, BKP_DAT34, BKP_DAT35, BKP_DAT36, BKP_DAT37,
    BKP_DAT38, BKP_DAT39, BKP_DAT40, BKP_DAT41
    /* DAT42：STOP2 唤醒戳，bkp_user 不用 */
};

#define BKP_USER_WORDS      ((uint16_t)(sizeof(s_dat) / sizeof(s_dat[0])))

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

static void bkp_clk(void)
{
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_PWR | RCC_APB1_PERIPH_BKP, ENABLE);
    PWR_BackupAccessEnable(ENABLE);
}

int bkp_user_init(void)
{
    bkp_clk();
    return 0;
}

int bkp_user_save(const void *data, uint16_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint16_t crc;
    uint16_t words;
    uint16_t i;
    uint16_t w;
    uint16_t hdr; /* magic + len in first word: magic | (len<<8) wait len is 16 */

    if ((data == 0) || (len == 0) || (len > BKP_USER_BYTES_MAX))
    {
        return -1;
    }
    /* word0 magic, word1 len, data..., last crc */
    words = (uint16_t)((len + 1u) / 2u);
    if ((2u + words + 1u) > BKP_USER_WORDS)
    {
        return -1;
    }

    bkp_clk();
    crc = crc16_ccitt(p, len);
    BKP_WriteBkpData(s_dat[0], BKP_USER_MAGIC);
    BKP_WriteBkpData(s_dat[1], len);
    for (i = 0; i < words; i++)
    {
        w = p[i * 2u];
        if ((uint16_t)(i * 2u + 1u) < len)
        {
            w |= (uint16_t)p[i * 2u + 1u] << 8;
        }
        BKP_WriteBkpData(s_dat[2u + i], w);
    }
    BKP_WriteBkpData(s_dat[2u + words], crc);
    (void)hdr;
    return 0;
}

int bkp_user_load(void *out, uint16_t out_max)
{
    uint8_t tmp[BKP_USER_BYTES_MAX];
    uint16_t magic;
    uint16_t len;
    uint16_t words;
    uint16_t i;
    uint16_t w;
    uint16_t crc;

    if ((out == 0) || (out_max == 0))
    {
        return -1;
    }

    bkp_clk();
    magic = BKP_ReadBkpData(s_dat[0]);
    len = BKP_ReadBkpData(s_dat[1]);
    if ((magic != BKP_USER_MAGIC) || (len == 0) || (len > BKP_USER_BYTES_MAX))
    {
        return -1;
    }
    words = (uint16_t)((len + 1u) / 2u);
    if ((2u + words + 1u) > BKP_USER_WORDS)
    {
        return -1;
    }

    memset(tmp, 0, sizeof(tmp));
    for (i = 0; i < words; i++)
    {
        w = BKP_ReadBkpData(s_dat[2u + i]);
        tmp[i * 2u] = (uint8_t)(w & 0xFFu);
        if ((uint16_t)(i * 2u + 1u) < len)
        {
            tmp[i * 2u + 1u] = (uint8_t)(w >> 8);
        }
    }
    crc = BKP_ReadBkpData(s_dat[2u + words]);
    if (crc16_ccitt(tmp, len) != crc)
    {
        return -1;
    }
    if (len > out_max)
    {
        return -1;
    }
    memcpy(out, tmp, len);
    return (int)len;
}
