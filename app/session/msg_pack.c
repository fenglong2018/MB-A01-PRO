/**
 * @file msg_pack.c
 * @brief MBA01 报文组包：现有字段填充，缺项 stub
 *
 * 缺：真实首次定位时间 / GNSS 日期→Unix / Flash 多点缓存（后补）
 * 本机卡号：RDSS `$BDICP` → cfg.device_id（见 BSP/rdss）
 */
#include "msg_pack.h"
#include "cfg.h"
#include "adc_bat.h"
#include "product_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void pad_u32(uint32_t v, char *out, int width)
{
    char tmp[12];
    int n;
    int i;

    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)v);
    n = (int)strlen(tmp);
    if (n > width)
    {
        n = width;
    }
    for (i = 0; i < width - n; i++)
    {
        out[i] = '0';
    }
    memcpy(out + (width - n), tmp, (size_t)n);
    out[width] = '\0';
}

/** 绝对值度 → 定点字符串：int_digits.frac_digits */
static void fmt_deg(double deg, char *out, int outsz, int int_digits, int frac_digits)
{
    int whole;
    double frac;
    unsigned long frac_u;
    double scale;
    int i;

    if (deg < 0)
    {
        deg = -deg;
    }
    whole = (int)deg;
    frac = deg - (double)whole;
    scale = 1.0;
    for (i = 0; i < frac_digits; i++)
    {
        scale *= 10.0;
    }
    frac_u = (unsigned long)(frac * scale + 0.5);
    if (frac_u >= (unsigned long)scale)
    {
        frac_u = 0;
        whole++;
    }
    snprintf(out, (size_t)outsz, "%0*d.%0*lu", int_digits, whole, frac_digits, frac_u);
}

static uint16_t pack_single(uint8_t *body, const gnss_fix_t *fix, uint8_t alarm_mode)
{
    char lon[16];
    char lat[16];
    char ts[12];
    double dlon;
    double dlat;
    char ew;
    char ns;
    uint16_t p = 0;

    if ((body == 0) || (fix == 0) || !fix->valid)
    {
        return 0;
    }
    if ((fix->lon_e7 == 0) || (fix->lat_e7 == 0))
    {
        return 0;
    }

    dlon = (double)fix->lon_e7 / 1e7;
    dlat = (double)fix->lat_e7 / 1e7;
    ew = (dlon >= 0) ? 'E' : 'W';
    ns = (dlat >= 0) ? 'N' : 'S';

    fmt_deg(dlon, lon, sizeof(lon), 3, 7);
    fmt_deg(dlat, lat, sizeof(lat), 2, 7);

    /* 时间戳 stub：无 RMC 日期时填 0（后补） */
    pad_u32(0, ts, 10);

    body[p++] = alarm_mode ? 'A' : 'N';
    memcpy(&body[p], ts, 10);
    p += 10;
    memcpy(&body[p], lon, 11);
    p += 11;
    body[p++] = ew;
    memcpy(&body[p], lat, 10);
    p += 10;
    body[p++] = ns;

    return (p == MSG_LOCADATA_LEN) ? MSG_LOCADATA_LEN : 0;
}

uint16_t msg_pack_loca_up(uint8_t *out, uint16_t out_max,
                          const gnss_fix_t *fix, uint8_t alarm_mode)
{
    uint8_t body[MSG_LOCADATA_LEN];
    char id[12];
    char first_ts[12];
    char bat[4];
    uint16_t blen;
    uint16_t p = 0;
    uint8_t pct = 0;

    if ((out == 0) || (out_max < MSG_PACK_MAX) || (fix == 0))
    {
        return 0;
    }

    blen = pack_single(body, fix, alarm_mode);
    if (blen == 0)
    {
        return 0;
    }

    pad_u32(cfg_get_device_id(), id, 10); /* RDSS $BDICP 成功后会同步到 cfg */
    pad_u32(0, first_ts, 10); /* 首次定位时间 stub */
#if USE_ADC_BAT
    pct = adc_bat_get_percent();
#else
    pct = 0;
#endif
    if (pct > 99)
    {
        pct = 99; /* MBA01 电量 2 位 */
    }
    snprintf(bat, sizeof(bat), "%02u", (unsigned)pct);

    memcpy(&out[p], id, 10);
    p += 10;
    memcpy(&out[p], first_ts, 10);
    p += 10;
    memcpy(&out[p], bat, 2);
    p += 2;
    memcpy(&out[p], body, blen);
    p += blen;
    return p;
}
