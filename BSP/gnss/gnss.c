/**
 * @file gnss.c
 * @brief ATGM336H：GGA 定位 + RMC 日期时刻；透传不解析。校时由 session 调用 rtc_post_unix。
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include <stdlib.h>

#include "config.h"
#include "gnss.h"
#include "pwr_plna.h"
#include "board_pins.h"
#include "stream.h"
#include "stream_ports.h"
#include "n32wb452_gpio.h"
#include "n32wb452_rcc.h"

#if USE_GNSS

#define GNSS_UART_NAME          "usart3"
#define GNSS_LINE_MAX           128

#define GNSS_CMD_START_FIX      1u
#define GNSS_CMD_PT_ENTER       2u
#define GNSS_CMD_PT_EXIT        3u
#define GNSS_CMD_BAT_PROTECT    4u

typedef struct
{
    uint8_t cmd;
} gnss_cmd_t;

typedef enum
{
    GNSS_ST_OFF = 0,
    GNSS_ST_FIX_WAIT,
    GNSS_ST_PASSTHRU,
} gnss_state_t;

static struct rt_thread s_thread;
static rt_uint8_t s_stack[GNSS_THREAD_STACK];
static struct rt_messagequeue s_cmd_mq;
static rt_uint8_t s_cmd_pool[GNSS_CMD_MQ_DEPTH * sizeof(gnss_cmd_t)];
static struct rt_messagequeue s_result_mq;
static rt_uint8_t s_result_pool[GNSS_RESULT_MQ_DEPTH * sizeof(gnss_msg_t)];

static rt_device_t s_uart;
static gnss_state_t s_state;
static gnss_fix_t s_last_fix;
static rt_tick_t s_fix_deadline;
static char s_line[GNSS_LINE_MAX];
static uint16_t s_line_len;
static uint8_t s_pwr_on;
static uint8_t s_lna_on;
static uint8_t s_gga_ok;
static gnss_fix_t s_pending_fix;
static uint32_t s_rmc_unix;

static void gnss_gpio_init(void)
{
    GPIO_InitType gpio;

    RCC_EnableAPB2PeriphClk(EN_LNA_POW_GNSS_CLK | EN_PGNSS_POW_CLK, ENABLE);

    GPIO_InitStruct(&gpio);
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    gpio.Pin = EN_LNA_POW_GNSS_PIN;
    GPIO_InitPeripheral(EN_LNA_POW_GNSS_PORT, &gpio);
    PIN_RESET(EN_LNA_POW_GNSS_PORT, EN_LNA_POW_GNSS_PIN);

    gpio.Pin = EN_PGNSS_POW_PIN;
    GPIO_InitPeripheral(EN_PGNSS_POW_PORT, &gpio);
    /* 高 = 关（低有效开） */
    PIN_SET(EN_PGNSS_POW_PORT, EN_PGNSS_POW_PIN);
}

static int gnss_uart_open(void)
{
    struct serial_configure cfg = RT_SERIAL_CONFIG_DEFAULT;

    if (s_uart == RT_NULL)
    {
        s_uart = rt_device_find(GNSS_UART_NAME);
        if (s_uart == RT_NULL)
        {
            rt_kprintf("[GNSS] %s not found\n", GNSS_UART_NAME);
            return -1;
        }
    }

    cfg.baud_rate = GNSS_UART_BAUD;
    cfg.data_bits = DATA_BITS_8;
    cfg.stop_bits = STOP_BITS_1;
    cfg.parity    = PARITY_NONE;
    cfg.bufsz     = 512;
    rt_device_control(s_uart, RT_DEVICE_CTRL_CONFIG, &cfg);

    if (rt_device_open(s_uart, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX) != RT_EOK)
    {
        rt_kprintf("[GNSS] uart open fail\n");
        return -1;
    }
    return 0;
}

static void gnss_uart_close(void)
{
    if (s_uart != RT_NULL)
    {
        rt_device_close(s_uart);
    }
}

static void gnss_power_on(void)
{
    if (s_pwr_on)
    {
        return;
    }

    pwr_plna_acquire();
    PIN_SET(EN_LNA_POW_GNSS_PORT, EN_LNA_POW_GNSS_PIN);
    s_lna_on = 1;
    PIN_RESET(EN_PGNSS_POW_PORT, EN_PGNSS_POW_PIN);
    s_pwr_on = 1;
    rt_thread_mdelay(GNSS_PWR_STABLE_MS);
    (void)gnss_uart_open();
    s_line_len = 0;
}

static void gnss_power_off(void)
{
    if (!s_pwr_on)
    {
        return;
    }

    gnss_uart_close();
    PIN_SET(EN_PGNSS_POW_PORT, EN_PGNSS_POW_PIN);
    if (s_lna_on)
    {
        PIN_RESET(EN_LNA_POW_GNSS_PORT, EN_LNA_POW_GNSS_PIN);
        s_lna_on = 0;
    }
    pwr_plna_release();
    s_pwr_on = 0;
    s_line_len = 0;
}

static int nmea_field(const char *line, int idx, char *out, int outlen)
{
    const char *p = line;
    int i = 0;

    if ((line == RT_NULL) || (out == RT_NULL) || (outlen < 2))
    {
        return -1;
    }
    if (*p == '$')
    {
        p++;
    }
    while (*p && i < idx)
    {
        if (*p == ',')
        {
            i++;
        }
        p++;
    }
    if (i != idx)
    {
        return -1;
    }

    i = 0;
    while (*p && (*p != ',') && (*p != '*') && (i < outlen - 1))
    {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 0;
}

/** ddmm.mmmm / dddmm.mmmm → 度*1e7 */
static int32_t nmea_deg_to_e7(const char *s, char hemi)
{
    double v;
    int deg;
    double minutes;
    char *dot;
    char tmp[24];
    int32_t e7;
    size_t n;

    if ((s == RT_NULL) || (s[0] == '\0'))
    {
        return 0;
    }
    n = strlen(s);
    if (n >= sizeof(tmp))
    {
        return 0;
    }
    memcpy(tmp, s, n + 1);
    dot = strchr(tmp, '.');
    if (dot == RT_NULL)
    {
        return 0;
    }
    /* 小数点前：纬度 4 位 ddmm，经度 5 位 dddmm */
    {
        int intlen = (int)(dot - tmp);
        int dlen = (intlen >= 5) ? 3 : 2;
        char dbuf[8];
        char mbuf[16];

        if (intlen <= dlen)
        {
            return 0;
        }
        memcpy(dbuf, tmp, (size_t)dlen);
        dbuf[dlen] = '\0';
        strncpy(mbuf, tmp + dlen, sizeof(mbuf) - 1);
        mbuf[sizeof(mbuf) - 1] = '\0';
        deg = atoi(dbuf);
        minutes = atof(mbuf);
    }
    v = (double)deg + minutes / 60.0;
    if ((hemi == 'S') || (hemi == 'W'))
    {
        v = -v;
    }
    e7 = (int32_t)(v * 10000000.0 + (v >= 0 ? 0.5 : -0.5));
    return e7;
}

static int parse_gga(const char *line, gnss_fix_t *fix)
{
    char talker[8];
    char fld[24];
    int quality;
    int sats;
    int alt_dm = 0x7FFF;

    if (nmea_field(line, 0, talker, sizeof(talker)) != 0)
    {
        return -1;
    }
    /* xxGGA */
    if ((strlen(talker) < 3) || (strcmp(talker + strlen(talker) - 3, "GGA") != 0))
    {
        return -1;
    }

    memset(fix, 0, sizeof(*fix));
    if (nmea_field(line, 1, fld, sizeof(fld)) == 0)
    {
        strncpy(fix->utc, fld, sizeof(fix->utc) - 1);
        fix->utc[sizeof(fix->utc) - 1] = '\0';
    }
    if (nmea_field(line, 6, fld, sizeof(fld)) != 0)
    {
        return -1;
    }
    quality = atoi(fld);
    fix->quality = (uint8_t)quality;
    if (quality < 1)
    {
        return 0; /* 语句有效但未定位 */
    }

    {
        char lat[24], ns[4], lon[24], ew[4];

        if ((nmea_field(line, 2, lat, sizeof(lat)) != 0) ||
            (nmea_field(line, 3, ns, sizeof(ns)) != 0) ||
            (nmea_field(line, 4, lon, sizeof(lon)) != 0) ||
            (nmea_field(line, 5, ew, sizeof(ew)) != 0))
        {
            return -1;
        }
        fix->lat_e7 = nmea_deg_to_e7(lat, ns[0]);
        fix->lon_e7 = nmea_deg_to_e7(lon, ew[0]);
    }

    if (nmea_field(line, 7, fld, sizeof(fld)) == 0)
    {
        sats = atoi(fld);
        fix->satellites = (uint8_t)((sats < 0) ? 0 : (sats > 255 ? 255 : sats));
    }
    if (nmea_field(line, 9, fld, sizeof(fld)) == 0 && fld[0])
    {
        double alt = atof(fld);
        alt_dm = (int)(alt * 10.0 + (alt >= 0 ? 0.5 : -0.5));
        if (alt_dm > 32767)
        {
            alt_dm = 32767;
        }
        if (alt_dm < -32768)
        {
            alt_dm = -32768;
        }
    }
    fix->alt_dm = (int16_t)alt_dm;
    fix->valid = 1;
    return 1;
}

static int is_leap_year(int y)
{
    return (((y % 4) == 0) && ((y % 100) != 0)) || ((y % 400) == 0);
}

/** UTC 年月日时分秒 → Unix；非法返回 0。不走 mktime（避免本地时区）。 */
static uint32_t utc_to_unix(int y, int mo, int d, int h, int mi, int s)
{
    static const uint8_t dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint32_t days = 0;
    int i;
    int md;

    if ((y < 2000) || (y > 2099) || (mo < 1) || (mo > 12) || (d < 1) ||
        (h < 0) || (h > 23) || (mi < 0) || (mi > 59) || (s < 0) || (s > 60))
    {
        return 0;
    }
    md = (int)dim[mo - 1];
    if ((mo == 2) && is_leap_year(y))
    {
        md = 29;
    }
    if (d > md)
    {
        return 0;
    }
    for (i = 1970; i < y; i++)
    {
        days += is_leap_year(i) ? 366u : 365u;
    }
    for (i = 1; i < mo; i++)
    {
        days += dim[i - 1];
        if ((i == 2) && is_leap_year(y))
        {
            days++;
        }
    }
    days += (uint32_t)(d - 1);
    return days * 86400u + (uint32_t)h * 3600u + (uint32_t)mi * 60u + (uint32_t)s;
}

static int parse_hhmmss(const char *s, int *h, int *mi, int *sec)
{
    if ((s == RT_NULL) || (strlen(s) < 6))
    {
        return -1;
    }
    *h = (s[0] - '0') * 10 + (s[1] - '0');
    *mi = (s[2] - '0') * 10 + (s[3] - '0');
    *sec = (s[4] - '0') * 10 + (s[5] - '0');
    if ((s[0] < '0') || (s[0] > '9') || (s[1] < '0') || (s[1] > '9') ||
        (s[2] < '0') || (s[2] > '9') || (s[3] < '0') || (s[3] > '9') ||
        (s[4] < '0') || (s[4] > '9') || (s[5] < '0') || (s[5] > '9'))
    {
        return -1;
    }
    return 0;
}

/**
 * xxRMC：status=A 且 date=ddmmyy → UTC Unix。
 * 返回 1=得到 unix，0=语句可忽略，-1=不是 RMC。
 */
static int parse_rmc_unix(const char *line, uint32_t *out_unix)
{
    char talker[8];
    char fld[24];
    char date[16];
    int h, mi, sec;
    int day, mon, year;
    uint32_t u;

    if (nmea_field(line, 0, talker, sizeof(talker)) != 0)
    {
        return -1;
    }
    if ((strlen(talker) < 3) || (strcmp(talker + strlen(talker) - 3, "RMC") != 0))
    {
        return -1;
    }
    if ((nmea_field(line, 2, fld, sizeof(fld)) != 0) || (fld[0] != 'A'))
    {
        return 0;
    }
    if ((nmea_field(line, 1, fld, sizeof(fld)) != 0) ||
        (parse_hhmmss(fld, &h, &mi, &sec) != 0))
    {
        return 0;
    }
    if ((nmea_field(line, 9, date, sizeof(date)) != 0) || (strlen(date) < 6))
    {
        return 0;
    }
    if ((date[0] < '0') || (date[0] > '9'))
    {
        return 0;
    }
    day = (date[0] - '0') * 10 + (date[1] - '0');
    mon = (date[2] - '0') * 10 + (date[3] - '0');
    year = 2000 + (date[4] - '0') * 10 + (date[5] - '0');
    if ((day == 0) && (mon == 0))
    {
        return 0;
    }
    u = utc_to_unix(year, mon, day, h, mi, sec);
    if (u == 0)
    {
        return 0;
    }
    *out_unix = u;
    return 1;
}

static void post_result(uint8_t reason, uint8_t ok, const gnss_fix_t *fix)
{
    gnss_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.reason = reason;
    msg.ok = ok;
    if (fix != RT_NULL)
    {
        msg.fix = *fix;
    }

    if (rt_mq_send(&s_result_mq, &msg, sizeof(msg)) != RT_EOK)
    {
        gnss_msg_t dump;
        rt_mq_recv(&s_result_mq, &dump, sizeof(dump), 0);
        rt_mq_send(&s_result_mq, &msg, sizeof(msg));
    }
}

static void gnss_abort_for_protect(void)
{
    if (s_state == GNSS_ST_FIX_WAIT)
    {
        post_result(GNSS_RESULT_ABORTED, 0, &s_last_fix);
        s_fix_deadline = 0;
    }
    if (s_pwr_on || (s_state != GNSS_ST_OFF))
    {
        gnss_power_off();
        s_state = GNSS_ST_OFF;
        rt_kprintf("[GNSS] abort (bat protect / FORCE_OFF)\n");
    }
}

static void finish_fix(uint8_t reason, uint8_t ok, const gnss_fix_t *fix)
{
    if (ok && (fix != RT_NULL) && fix->valid)
    {
        s_last_fix = *fix;
    }
    post_result(reason, ok, ok ? fix : &s_last_fix);
    gnss_power_off();
    s_state = GNSS_ST_OFF;
    s_fix_deadline = 0;
    s_gga_ok = 0;
    s_rmc_unix = 0;
    rt_kprintf("[GNSS] fix done reason=%u ok=%u unix=%lu\n",
               reason, ok, (unsigned long)(ok && fix ? fix->unix_sec : 0u));
}

static void try_finish_fix(void)
{
    if (!s_gga_ok)
    {
        return;
    }
    if (s_rmc_unix == 0)
    {
        return; /* 等 RMC；超时再无日期关电 */
    }
    s_pending_fix.unix_sec = s_rmc_unix;
    finish_fix(GNSS_RESULT_OK, 1, &s_pending_fix);
}

static void on_line(const char *line)
{
    gnss_fix_t fix;
    uint32_t rmc_unix = 0;
    int r;

    if (s_state == GNSS_ST_PASSTHRU)
    {
        if (stream_is_enabled(STREAM_NAME_GNSS) > 0)
        {
            size_t n = strlen(line);
            char crlf[GNSS_LINE_MAX + 2];

            if (n + 2 < sizeof(crlf))
            {
                memcpy(crlf, line, n);
                crlf[n] = '\r';
                crlf[n + 1] = '\n';
                (void)stream_write(STREAM_NAME_GNSS, (const uint8_t *)crlf, (uint32_t)(n + 2));
            }
        }
        return;
    }

    if (s_state != GNSS_ST_FIX_WAIT)
    {
        return;
    }

    r = parse_rmc_unix(line, &rmc_unix);
    if (r == 1)
    {
        s_rmc_unix = rmc_unix;
        try_finish_fix();
        return;
    }

    r = parse_gga(line, &fix);
    if (r == 1)
    {
        s_pending_fix = fix;
        s_pending_fix.unix_sec = s_rmc_unix;
        s_gga_ok = 1;
        try_finish_fix();
    }
}

static void feed_byte(uint8_t b)
{
    if ((b == '\r') || (b == '\n'))
    {
        if (s_line_len > 0)
        {
            s_line[s_line_len] = '\0';
            on_line(s_line);
            s_line_len = 0;
        }
        return;
    }
    if (s_line_len + 1 < GNSS_LINE_MAX)
    {
        s_line[s_line_len++] = (char)b;
    }
    else
    {
        s_line_len = 0; /* 溢出丢弃本行 */
    }
}

static void poll_uart(void)
{
    uint8_t buf[64];
    rt_size_t n;

    if ((s_uart == RT_NULL) || !s_pwr_on)
    {
        return;
    }

    n = rt_device_read(s_uart, 0, buf, sizeof(buf));
    while (n > 0)
    {
        rt_size_t i;
        for (i = 0; i < n; i++)
        {
            feed_byte(buf[i]);
        }
        n = rt_device_read(s_uart, 0, buf, sizeof(buf));
    }
}

static void handle_cmd(const gnss_cmd_t *cmd)
{
    switch (cmd->cmd)
    {
    case GNSS_CMD_START_FIX:
        if (s_state == GNSS_ST_PASSTHRU)
        {
            rt_kprintf("[GNSS] start_fix ignored (passthru)\n");
            post_result(GNSS_RESULT_ABORTED, 0, &s_last_fix);
            break;
        }
        if (s_state == GNSS_ST_FIX_WAIT)
        {
            /* 已在定位中：忽略重复请求 */
            break;
        }
        gnss_power_on();
        s_state = GNSS_ST_FIX_WAIT;
        s_gga_ok = 0;
        s_rmc_unix = 0;
        memset(&s_pending_fix, 0, sizeof(s_pending_fix));
        s_fix_deadline = rt_tick_get() + rt_tick_from_millisecond(GNSS_FIX_TIMEOUT_MS);
        rt_kprintf("[GNSS] fix start timeout=%ums\n", (unsigned)GNSS_FIX_TIMEOUT_MS);
        break;

    case GNSS_CMD_PT_ENTER:
        if (s_state == GNSS_ST_FIX_WAIT)
        {
            post_result(GNSS_RESULT_ABORTED, 0, &s_last_fix);
            s_fix_deadline = 0;
        }
        gnss_power_on();
        s_state = GNSS_ST_PASSTHRU;
        rt_kprintf("[GNSS] passthru enter\n");
        break;

    case GNSS_CMD_PT_EXIT:
        if (s_state == GNSS_ST_PASSTHRU)
        {
            gnss_power_off();
            s_state = GNSS_ST_OFF;
            rt_kprintf("[GNSS] passthru exit\n");
        }
        break;

    case GNSS_CMD_BAT_PROTECT:
        gnss_abort_for_protect();
        break;

    default:
        break;
    }
}

static void gnss_thread_entry(void *param)
{
    gnss_cmd_t cmd;
    rt_int32_t wait;

    (void)param;

    while (1)
    {
        if (s_state == GNSS_ST_FIX_WAIT)
        {
            rt_tick_t now = rt_tick_get();
            rt_int32_t left = (rt_int32_t)(s_fix_deadline - now);
            wait = (left > 0) ? left : 0;
            if (wait > rt_tick_from_millisecond(50))
            {
                wait = rt_tick_from_millisecond(50);
            }
        }
        else if (s_state == GNSS_ST_PASSTHRU)
        {
            wait = rt_tick_from_millisecond(20);
        }
        else
        {
            wait = RT_WAITING_FOREVER;
        }

        if (rt_mq_recv(&s_cmd_mq, &cmd, sizeof(cmd), wait) == RT_EOK)
        {
            handle_cmd(&cmd);
        }

        poll_uart();

        if ((s_state == GNSS_ST_FIX_WAIT) &&
            ((rt_int32_t)(s_fix_deadline - rt_tick_get()) <= 0))
        {
            if (s_gga_ok)
            {
                s_pending_fix.unix_sec = s_rmc_unix;
                finish_fix(GNSS_RESULT_OK, 1, &s_pending_fix);
            }
            else
            {
                finish_fix(GNSS_RESULT_TIMEOUT, 0, &s_last_fix);
            }
        }
    }
}

static int gnss_post_cmd(uint8_t c)
{
    gnss_cmd_t cmd;

    cmd.cmd = c;
    if (rt_mq_send(&s_cmd_mq, &cmd, sizeof(cmd)) != RT_EOK)
    {
        gnss_cmd_t dump;
        rt_mq_recv(&s_cmd_mq, &dump, sizeof(dump), 0);
        if (rt_mq_send(&s_cmd_mq, &cmd, sizeof(cmd)) != RT_EOK)
        {
            return -RT_ERROR;
        }
    }
    return RT_EOK;
}

int gnss_start_fix(void)
{
    if (s_state == GNSS_ST_PASSTHRU)
    {
        return -RT_EBUSY;
    }
    return gnss_post_cmd(GNSS_CMD_START_FIX);
}

int gnss_passthru_enter(void)
{
    return gnss_post_cmd(GNSS_CMD_PT_ENTER);
}

void gnss_on_bat_protect(void)
{
    (void)gnss_post_cmd(GNSS_CMD_BAT_PROTECT);
}

int gnss_passthru_exit(void)
{
    return gnss_post_cmd(GNSS_CMD_PT_EXIT);
}

int gnss_passthru_active(void)
{
    return (s_state == GNSS_ST_PASSTHRU) ? 1 : 0;
}

int gnss_passthru_write(const uint8_t *data, uint32_t len)
{
    if ((data == RT_NULL) || (len == 0))
    {
        return -RT_EINVAL;
    }
    if ((s_state != GNSS_ST_PASSTHRU) || (s_uart == RT_NULL) || !s_pwr_on)
    {
        return -RT_ERROR;
    }
    return (int)rt_device_write(s_uart, 0, data, len);
}

int gnss_get_fix(gnss_fix_t *out)
{
    if (out == RT_NULL)
    {
        return -RT_EINVAL;
    }
    *out = s_last_fix;
    return s_last_fix.valid ? RT_EOK : -RT_ERROR;
}

struct rt_messagequeue *gnss_result_mq(void)
{
    return &s_result_mq;
}

int gnss_init(void)
{
    rt_err_t err;

    memset(&s_last_fix, 0, sizeof(s_last_fix));
    s_state = GNSS_ST_OFF;
    s_pwr_on = 0;
    s_lna_on = 0;
    s_line_len = 0;
    s_fix_deadline = 0;
    s_gga_ok = 0;
    s_rmc_unix = 0;
    s_uart = RT_NULL;

    pwr_plna_init();
    gnss_gpio_init();

    rt_mq_init(&s_cmd_mq, "gnsscmd", s_cmd_pool, sizeof(gnss_cmd_t),
               sizeof(s_cmd_pool), RT_IPC_FLAG_FIFO);
    rt_mq_init(&s_result_mq, "gnssres", s_result_pool, sizeof(gnss_msg_t),
               sizeof(s_result_pool), RT_IPC_FLAG_FIFO);

    err = rt_thread_init(&s_thread, "gnss", gnss_thread_entry, RT_NULL,
                         s_stack, sizeof(s_stack), GNSS_THREAD_PRIO, 10);
    if (err != RT_EOK)
    {
        rt_kprintf("[GNSS] thread init fail\n");
        return -1;
    }
    rt_thread_startup(&s_thread);
    rt_kprintf("[GNSS] ready baud=%u\n", (unsigned)GNSS_UART_BAUD);
    return 0;
}

static int app_gnss_init(void)
{
    return gnss_init();
}
INIT_APP_EXPORT(app_gnss_init);

#endif /* USE_GNSS */
