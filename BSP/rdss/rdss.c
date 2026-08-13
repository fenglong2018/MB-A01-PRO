/**
 * @file rdss.c
 * @brief TD3050：电源 / PWI 波束 / 可选 PA / CCTCQ / FKI / CCICR·BDICP / 透传
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "rdss.h"
#include "pwr_plna.h"
#include "cfg.h"
#include "board_pins.h"
#include "stream.h"
#include "stream_ports.h"
#include "n32wb452_gpio.h"
#include "n32wb452_rcc.h"

#if USE_RDSS

#define RDSS_UART_NAME          "usart2"
#define RDSS_LINE_MAX           256
#define RDSS_CMD_MQ_DEPTH       4
#define RDSS_RESULT_MQ_DEPTH    4

#define RDSS_CMD_START_SEND     1u
#define RDSS_CMD_PT_ENTER       2u
#define RDSS_CMD_PT_EXIT        3u
#define RDSS_CMD_BAT_PROTECT    4u
#define RDSS_CMD_ENSURE_CARD    5u

/* TD3050：$CCICR,0,00*68 — 本机及 IC 查询，返回 $BDICP */
#define RDSS_CCICR_CMD          "$CCICR,0,00*68\r\n"
#define RDSS_CCICR_CMD_LEN      16

typedef struct
{
    uint8_t  cmd;
    uint16_t len;
    uint8_t  payload[RDSS_TX_PAYLOAD_MAX];
} rdss_cmd_t;

typedef enum
{
    RDSS_ST_OFF = 0,
    RDSS_ST_CARD_WAIT,
    RDSS_ST_BEAM_WAIT,
    RDSS_ST_TX_WAIT,
    RDSS_ST_PASSTHRU,
} rdss_state_t;

static struct rt_thread s_thread;
static rt_uint8_t s_stack[RDSS_THREAD_STACK];
static struct rt_messagequeue s_cmd_mq;
static rt_uint8_t s_cmd_pool[RDSS_CMD_MQ_DEPTH * sizeof(rdss_cmd_t)];
static struct rt_messagequeue s_result_mq;
static rt_uint8_t s_result_pool[RDSS_RESULT_MQ_DEPTH * sizeof(rdss_msg_t)];

static rt_device_t s_uart;
static rdss_state_t s_state;
static rt_tick_t s_deadline;
static char s_line[RDSS_LINE_MAX];
static uint16_t s_line_len;
static uint8_t s_pwr_on;
static uint8_t s_pa_on;
static uint8_t s_beam_ok;
static uint8_t s_tx_payload[RDSS_TX_PAYLOAD_MAX];
static uint16_t s_tx_len;
static uint32_t s_card_id;
static struct rt_semaphore s_card_sem;
static uint8_t s_card_waiting; /* ensure_card 阻塞中 */

static void rdss_gpio_init(void)
{
    GPIO_InitType gpio;

    RCC_EnableAPB2PeriphClk(EN_LNA_RDSS_POW_CLK | EN_PRDSS_POW_CLK |
                            EN_5V_PA_POW_CLK | RCC_APB2_PERIPH_AFIO, ENABLE);

    /* PB3=PRDSS 默认 JTDO：释放 JTAG 仅保留 SWD */
    GPIO_ConfigPinRemap(GPIO_RMP_SW_JTAG_SW_ENABLE, ENABLE);

    GPIO_InitStruct(&gpio);
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    gpio.Pin = EN_LNA_RDSS_POW_PIN;
    GPIO_InitPeripheral(EN_LNA_RDSS_POW_PORT, &gpio);
    PIN_RESET(EN_LNA_RDSS_POW_PORT, EN_LNA_RDSS_POW_PIN); /* 高开，默认关 */

    gpio.Pin = EN_PRDSS_POW_PIN;
    GPIO_InitPeripheral(EN_PRDSS_POW_PORT, &gpio);
    PIN_SET(EN_PRDSS_POW_PORT, EN_PRDSS_POW_PIN); /* 低开，默认关 */

    gpio.Pin = EN_5V_PA_POW_PIN;
    GPIO_InitPeripheral(EN_5V_PA_POW_PORT, &gpio);
    PIN_SET(EN_5V_PA_POW_PORT, EN_5V_PA_POW_PIN); /* 低开 PA，默认关 */
}

static int rdss_uart_open(void)
{
    struct serial_configure cfg = RT_SERIAL_CONFIG_DEFAULT;

    if (s_uart == RT_NULL)
    {
        s_uart = rt_device_find(RDSS_UART_NAME);
        if (s_uart == RT_NULL)
        {
            rt_kprintf("[RDSS] %s not found\n", RDSS_UART_NAME);
            return -1;
        }
    }

    cfg.baud_rate = RDSS_UART_BAUD;
    cfg.data_bits = DATA_BITS_8;
    cfg.stop_bits = STOP_BITS_1;
    cfg.parity    = PARITY_NONE;
    cfg.bufsz     = 512;
    rt_device_control(s_uart, RT_DEVICE_CTRL_CONFIG, &cfg);

    if (rt_device_open(s_uart, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX) != RT_EOK)
    {
        rt_kprintf("[RDSS] uart open fail\n");
        return -1;
    }
    return 0;
}

static void rdss_uart_close(void)
{
    if (s_uart != RT_NULL)
    {
        rt_device_close(s_uart);
    }
}

static void rdss_pa_off(void)
{
    if (s_pa_on)
    {
        PIN_SET(EN_5V_PA_POW_PORT, EN_5V_PA_POW_PIN);
        s_pa_on = 0;
    }
}

static void rdss_pa_on_if_cfg(void)
{
    if (cfg_get_pa_enable() && !s_pa_on)
    {
        PIN_RESET(EN_5V_PA_POW_PORT, EN_5V_PA_POW_PIN);
        s_pa_on = 1;
        rt_kprintf("[RDSS] PA on\n");
    }
    else if (!cfg_get_pa_enable())
    {
        rt_kprintf("[RDSS] PA skipped (cfg pa_enable=0)\n");
    }
}

static void rdss_power_on(void)
{
    if (s_pwr_on)
    {
        return;
    }

    pwr_plna_acquire();
    PIN_SET(EN_LNA_RDSS_POW_PORT, EN_LNA_RDSS_POW_PIN);
    PIN_RESET(EN_PRDSS_POW_PORT, EN_PRDSS_POW_PIN);
    s_pwr_on = 1;
    rt_thread_mdelay(RDSS_PWR_STABLE_MS);
    (void)rdss_uart_open();
    s_line_len = 0;
    s_beam_ok = 0;
}

static void rdss_power_off(void)
{
    if (!s_pwr_on)
    {
        return;
    }

    rdss_pa_off();
    rdss_uart_close();
    PIN_SET(EN_PRDSS_POW_PORT, EN_PRDSS_POW_PIN);
    PIN_RESET(EN_LNA_RDSS_POW_PORT, EN_LNA_RDSS_POW_PIN);
    pwr_plna_release();
    s_pwr_on = 0;
    s_line_len = 0;
    s_beam_ok = 0;
}

static void post_result(uint8_t reason, uint8_t ok)
{
    rdss_msg_t msg;

    msg.reason = reason;
    msg.ok = ok;
    if (rt_mq_send(&s_result_mq, &msg, sizeof(msg)) != RT_EOK)
    {
        rdss_msg_t dump;
        rt_mq_recv(&s_result_mq, &dump, sizeof(dump), 0);
        (void)rt_mq_send(&s_result_mq, &msg, sizeof(msg));
    }
}

static void card_wait_done(void)
{
    if (s_card_waiting)
    {
        s_card_waiting = 0;
        rt_sem_release(&s_card_sem);
    }
}

static int rdss_send_ccicr(void)
{
    if ((s_uart == RT_NULL) || !s_pwr_on)
    {
        return -1;
    }
    rt_kprintf("[RDSS] CCICR\n");
    return (int)rt_device_write(s_uart, 0, RDSS_CCICR_CMD, RDSS_CCICR_CMD_LEN);
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

/** $BDPWI：波束≥1、时间>20、至少一路 S2C_d>40 */
static void on_bdpwi(const char *line)
{
    char f[16];
    double t = 0;
    int beam_n = 0;
    int i;
    int good = 0;

    if (nmea_field(line, 1, f, sizeof(f)) == 0)
    {
        t = atof(f);
    }
    if (nmea_field(line, 3, f, sizeof(f)) == 0)
    {
        beam_n = atoi(f);
    }
    if ((beam_n < 1) || (t <= (double)RDSS_PWI_TIME_MIN))
    {
        s_beam_ok = 0;
        return;
    }
    if (beam_n > 10)
    {
        beam_n = 10;
    }

    for (i = 0; i < beam_n; i++)
    {
        int cnr;
        /* 字段：4+i*3 = 编号，5+i*3 = S2C_d */
        if (nmea_field(line, 5 + i * 3, f, sizeof(f)) != 0)
        {
            continue;
        }
        cnr = atoi(f);
        if (cnr > RDSS_CNR_MIN)
        {
            good++;
        }
    }

    if (good >= 1)
    {
        s_beam_ok = 1;
        rt_kprintf("[RDSS] beam ok n=%d good=%d t=%.1f\n", beam_n, good, t);
    }
}

/** $BDFKI：第 3 字段 Y/N */
static int on_bdfki(const char *line)
{
    char f[8];

    if (nmea_field(line, 3, f, sizeof(f)) != 0)
    {
        return 0;
    }
    return (f[0] == 'Y' || f[0] == 'y') ? 1 : 0;
}

/** $BDICP：字段1 = 用户地址(ID) = 本机卡号（TD3050 接口协议） */
static void on_bdicp(const char *line)
{
    char f[16];
    uint32_t id;

    if (nmea_field(line, 1, f, sizeof(f)) != 0)
    {
        return;
    }
    if (f[0] == '\0')
    {
        return;
    }
    id = (uint32_t)strtoul(f, NULL, 10);
    if (id == 0)
    {
        return;
    }

    s_card_id = id;
    cfg_set_device_id(id);
    rt_kprintf("[RDSS] card=%lu\n", (unsigned long)id);

    if (s_state == RDSS_ST_CARD_WAIT)
    {
        rdss_power_off();
        s_state = RDSS_ST_OFF;
        card_wait_done();
    }
}

static uint8_t xor_sum(const uint8_t *buf, int len)
{
    uint8_t s = 0;
    int i;

    for (i = 0; i < len; i++)
    {
        s ^= buf[i];
    }
    return s;
}

static void byte_to_hex2(uint8_t b, char *out)
{
    static const char hex[] = "0123456789ABCDEF";
    out[0] = hex[(b >> 4) & 0xF];
    out[1] = hex[b & 0xF];
}

static int ascii_to_hexstr(const uint8_t *in, uint16_t inlen, char *out, int outmax)
{
    uint16_t i;
    int p = 0;

    if ((inlen * 2) >= (uint16_t)outmax)
    {
        return -1;
    }
    for (i = 0; i < inlen; i++)
    {
        byte_to_hex2(in[i], &out[p]);
        p += 2;
    }
    out[p] = '\0';
    return p;
}

static int rdss_send_cctcq(uint32_t recv_id, const uint8_t *payload, uint16_t plen)
{
    char hex[RDSS_TX_PAYLOAD_MAX * 2 + 4];
    char frame[RDSS_TX_PAYLOAD_MAX * 2 + 96];
    char chk[3];
    int hexlen;
    int body_len;
    uint8_t sum;
    int n;

    hexlen = ascii_to_hexstr(payload, plen, hex, (int)sizeof(hex));
    if (hexlen < 0)
    {
        return -1;
    }

    /* $CCTCQ,<id>,2,1,2,<hex>,0*XX\r\n — MBA01 同参 */
    n = snprintf(frame, sizeof(frame),
                 "$CCTCQ,%lu,2,1,2,%s,0*",
                 (unsigned long)recv_id, hex);
    if ((n < 0) || (n >= (int)sizeof(frame) - 4))
    {
        return -1;
    }

    body_len = n - 2; /* 不含 '$' 与 '*'：NMEA 异或区间 */
    sum = xor_sum((const uint8_t *)&frame[1], body_len);
    byte_to_hex2(sum, chk);
    chk[2] = '\0';
    n += snprintf(frame + n, sizeof(frame) - (size_t)n, "%s\r\n", chk);

    rt_kprintf("[RDSS] CCTCQ len=%d\n", n);
    if ((s_uart == RT_NULL) || !s_pwr_on)
    {
        return -1;
    }
    return (int)rt_device_write(s_uart, 0, frame, (rt_size_t)n);
}

static void on_line(const char *line)
{
    const char *p = line;
    char tmp[RDSS_LINE_MAX];
    const char *use = line;

    if (s_state == RDSS_ST_PASSTHRU)
    {
        (void)stream_write(STREAM_NAME_RDSS, (const uint8_t *)line, (uint32_t)strlen(line));
        (void)stream_write(STREAM_NAME_RDSS, (const uint8_t *)"\n", 1);
        return;
    }

    if (*p == '$')
    {
        p++;
    }
    else
    {
        tmp[0] = '$';
        strncpy(tmp + 1, line, sizeof(tmp) - 2);
        tmp[sizeof(tmp) - 1] = '\0';
        use = tmp;
    }

    if (strncmp(p, "BDICP", 5) == 0)
    {
        on_bdicp(use);
        return;
    }

    if (strncmp(p, "BDPWI", 5) == 0)
    {
        on_bdpwi(use);
        return;
    }

    if ((s_state == RDSS_ST_TX_WAIT) && (strncmp(p, "BDFKI", 5) == 0))
    {
        int ok = on_bdfki(use);
        if (ok)
        {
            rt_kprintf("[RDSS] FKI Y\n");
            rdss_power_off();
            s_state = RDSS_ST_OFF;
            post_result(RDSS_RESULT_OK, 1);
        }
        else
        {
            rt_kprintf("[RDSS] FKI N\n");
            rdss_power_off();
            s_state = RDSS_ST_OFF;
            post_result(RDSS_RESULT_FKI_FAIL, 0);
        }
    }
}

static void feed_byte(uint8_t b)
{
    if (s_state == RDSS_ST_PASSTHRU)
    {
        (void)stream_write(STREAM_NAME_RDSS, &b, 1);
        return;
    }

    if (b == '\r')
    {
        return;
    }
    if (b == '\n')
    {
        if (s_line_len > 0)
        {
            s_line[s_line_len] = '\0';
            on_line(s_line);
            s_line_len = 0;
        }
        return;
    }
    if (s_line_len + 1 < RDSS_LINE_MAX)
    {
        s_line[s_line_len++] = (char)b;
    }
    else
    {
        s_line_len = 0;
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

static void abort_session(uint8_t reason)
{
    if ((s_state == RDSS_ST_BEAM_WAIT) || (s_state == RDSS_ST_TX_WAIT))
    {
        rdss_power_off();
        s_state = RDSS_ST_OFF;
        post_result(reason, 0);
    }
    else if (s_state == RDSS_ST_CARD_WAIT)
    {
        rdss_power_off();
        s_state = RDSS_ST_OFF;
        card_wait_done();
    }
    else if (s_state == RDSS_ST_PASSTHRU)
    {
        rdss_power_off();
        s_state = RDSS_ST_OFF;
    }
}

static void handle_cmd(const rdss_cmd_t *cmd)
{
    switch (cmd->cmd)
    {
    case RDSS_CMD_START_SEND:
        if (s_state == RDSS_ST_PASSTHRU)
        {
            post_result(RDSS_RESULT_BUSY, 0);
            break;
        }
        if ((s_state == RDSS_ST_BEAM_WAIT) || (s_state == RDSS_ST_TX_WAIT) ||
            (s_state == RDSS_ST_CARD_WAIT))
        {
            post_result(RDSS_RESULT_BUSY, 0);
            break;
        }
        if ((cmd->len == 0) || (cmd->len > RDSS_TX_PAYLOAD_MAX))
        {
            post_result(RDSS_RESULT_PARAM, 0);
            break;
        }
        memcpy(s_tx_payload, cmd->payload, cmd->len);
        s_tx_len = cmd->len;
        rdss_power_on();
        (void)rdss_send_ccicr();
        s_state = RDSS_ST_BEAM_WAIT;
        s_beam_ok = 0;
        s_deadline = rt_tick_get() + rt_tick_from_millisecond(RDSS_BEAM_TIMEOUT_MS);
        rt_kprintf("[RDSS] beam wait %ums\n", (unsigned)RDSS_BEAM_TIMEOUT_MS);
        break;

    case RDSS_CMD_ENSURE_CARD:
        if (s_card_id != 0)
        {
            card_wait_done();
            break;
        }
        if ((s_state == RDSS_ST_BEAM_WAIT) || (s_state == RDSS_ST_TX_WAIT) ||
            (s_state == RDSS_ST_PASSTHRU) || (s_state == RDSS_ST_CARD_WAIT))
        {
            card_wait_done();
            break;
        }
        rdss_power_on();
        (void)rdss_send_ccicr();
        s_state = RDSS_ST_CARD_WAIT;
        s_deadline = rt_tick_get() + rt_tick_from_millisecond(RDSS_CARD_TIMEOUT_MS);
        rt_kprintf("[RDSS] card wait %ums\n", (unsigned)RDSS_CARD_TIMEOUT_MS);
        break;

    case RDSS_CMD_PT_ENTER:
        if ((s_state == RDSS_ST_BEAM_WAIT) || (s_state == RDSS_ST_TX_WAIT))
        {
            post_result(RDSS_RESULT_ABORTED, 0);
        }
        if (s_state == RDSS_ST_CARD_WAIT)
        {
            card_wait_done();
        }
        rdss_power_on();
        (void)rdss_send_ccicr();
        s_state = RDSS_ST_PASSTHRU;
        rt_kprintf("[RDSS] passthru enter\n");
        break;

    case RDSS_CMD_PT_EXIT:
        if (s_state == RDSS_ST_PASSTHRU)
        {
            rdss_power_off();
            s_state = RDSS_ST_OFF;
            rt_kprintf("[RDSS] passthru exit\n");
        }
        break;

    case RDSS_CMD_BAT_PROTECT:
        abort_session(RDSS_RESULT_ABORTED);
        if (s_state == RDSS_ST_PASSTHRU)
        {
            rdss_power_off();
            s_state = RDSS_ST_OFF;
        }
        break;

    default:
        break;
    }
}

static void rdss_thread_entry(void *param)
{
    rdss_cmd_t cmd;
    rt_int32_t wait;

    (void)param;

    while (1)
    {
        if ((s_state == RDSS_ST_BEAM_WAIT) || (s_state == RDSS_ST_TX_WAIT) ||
            (s_state == RDSS_ST_CARD_WAIT))
        {
            rt_int32_t left = (rt_int32_t)(s_deadline - rt_tick_get());
            wait = (left > 0) ? left : 0;
            if (wait > rt_tick_from_millisecond(50))
            {
                wait = rt_tick_from_millisecond(50);
            }
        }
        else if (s_state == RDSS_ST_PASSTHRU)
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

        if ((s_state == RDSS_ST_BEAM_WAIT) && s_beam_ok)
        {
            rdss_pa_on_if_cfg();
            if (rdss_send_cctcq(cfg_get_recv_id(), s_tx_payload, s_tx_len) < 0)
            {
                rdss_power_off();
                s_state = RDSS_ST_OFF;
                post_result(RDSS_RESULT_PARAM, 0);
            }
            else
            {
                s_state = RDSS_ST_TX_WAIT;
                s_deadline = rt_tick_get() + rt_tick_from_millisecond(RDSS_FKI_TIMEOUT_MS);
                rt_kprintf("[RDSS] wait FKI\n");
            }
        }

        if (((s_state == RDSS_ST_BEAM_WAIT) || (s_state == RDSS_ST_TX_WAIT) ||
             (s_state == RDSS_ST_CARD_WAIT)) &&
            ((rt_int32_t)(s_deadline - rt_tick_get()) <= 0))
        {
            if (s_state == RDSS_ST_CARD_WAIT)
            {
                rt_kprintf("[RDSS] card timeout\n");
                rdss_power_off();
                s_state = RDSS_ST_OFF;
                card_wait_done();
            }
            else if (s_state == RDSS_ST_BEAM_WAIT)
            {
                rt_kprintf("[RDSS] beam timeout\n");
                rdss_power_off();
                s_state = RDSS_ST_OFF;
                post_result(RDSS_RESULT_BEAM_TO, 0);
            }
            else
            {
                rt_kprintf("[RDSS] FKI timeout\n");
                rdss_power_off();
                s_state = RDSS_ST_OFF;
                post_result(RDSS_RESULT_FKI_FAIL, 0);
            }
        }
    }
}

static int rdss_post_cmd(const rdss_cmd_t *cmd)
{
    if (rt_mq_send(&s_cmd_mq, (void *)cmd, sizeof(*cmd)) != RT_EOK)
    {
        rdss_cmd_t dump;
        rt_mq_recv(&s_cmd_mq, &dump, sizeof(dump), 0);
        if (rt_mq_send(&s_cmd_mq, (void *)cmd, sizeof(*cmd)) != RT_EOK)
        {
            return -RT_ERROR;
        }
    }
    return RT_EOK;
}

int rdss_start_send(const uint8_t *payload, uint16_t len)
{
    rdss_cmd_t cmd;

    if ((payload == RT_NULL) || (len == 0) || (len > RDSS_TX_PAYLOAD_MAX))
    {
        return -RT_EINVAL;
    }
    if (s_state == RDSS_ST_PASSTHRU)
    {
        return -RT_EBUSY;
    }
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = RDSS_CMD_START_SEND;
    cmd.len = len;
    memcpy(cmd.payload, payload, len);
    return rdss_post_cmd(&cmd);
}

uint32_t rdss_get_card_id(void)
{
    return s_card_id;
}

int rdss_ensure_card(uint32_t timeout_ms)
{
    rdss_cmd_t cmd;
    rt_int32_t tick;

    if (s_card_id != 0)
    {
        return RT_EOK;
    }
    if (timeout_ms == 0)
    {
        timeout_ms = RDSS_CARD_TIMEOUT_MS;
    }

    /* 排空残留，避免上次超时后多一次 release */
    while (rt_sem_trytake(&s_card_sem) == RT_EOK)
    {
    }

    s_card_waiting = 1;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = RDSS_CMD_ENSURE_CARD;
    if (rdss_post_cmd(&cmd) != RT_EOK)
    {
        s_card_waiting = 0;
        return -RT_ERROR;
    }

    tick = rt_tick_from_millisecond(timeout_ms + 500u);
    if (rt_sem_take(&s_card_sem, tick) != RT_EOK)
    {
        s_card_waiting = 0;
        return -RT_ETIMEOUT;
    }
    return (s_card_id != 0) ? RT_EOK : -RT_ETIMEOUT;
}

int rdss_passthru_enter(void)
{
    rdss_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = RDSS_CMD_PT_ENTER;
    return rdss_post_cmd(&cmd);
}

int rdss_passthru_exit(void)
{
    rdss_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = RDSS_CMD_PT_EXIT;
    return rdss_post_cmd(&cmd);
}

void rdss_on_bat_protect(void)
{
    rdss_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = RDSS_CMD_BAT_PROTECT;
    (void)rdss_post_cmd(&cmd);
}

int rdss_passthru_active(void)
{
    return (s_state == RDSS_ST_PASSTHRU) ? 1 : 0;
}

int rdss_passthru_write(const uint8_t *data, uint32_t len)
{
    if ((data == RT_NULL) || (len == 0))
    {
        return -RT_EINVAL;
    }
    if ((s_state != RDSS_ST_PASSTHRU) || (s_uart == RT_NULL) || !s_pwr_on)
    {
        return -RT_ERROR;
    }
    return (int)rt_device_write(s_uart, 0, data, len);
}

struct rt_messagequeue *rdss_result_mq(void)
{
    return &s_result_mq;
}

int rdss_init(void)
{
    rt_err_t err;

    s_state = RDSS_ST_OFF;
    s_pwr_on = 0;
    s_pa_on = 0;
    s_line_len = 0;
    s_beam_ok = 0;
    s_uart = RT_NULL;
    s_tx_len = 0;
    s_card_id = 0;
    s_card_waiting = 0;

    pwr_plna_init();
    rdss_gpio_init();

    rt_sem_init(&s_card_sem, "rdsscrd", 0, RT_IPC_FLAG_FIFO);
    rt_mq_init(&s_cmd_mq, "rdsscmd", s_cmd_pool, sizeof(rdss_cmd_t),
               sizeof(s_cmd_pool), RT_IPC_FLAG_FIFO);
    rt_mq_init(&s_result_mq, "rdssres", s_result_pool, sizeof(rdss_msg_t),
               sizeof(s_result_pool), RT_IPC_FLAG_FIFO);

    err = rt_thread_init(&s_thread, "rdss", rdss_thread_entry, RT_NULL,
                         s_stack, sizeof(s_stack), RDSS_THREAD_PRIO, 10);
    if (err != RT_EOK)
    {
        rt_kprintf("[RDSS] thread init fail\n");
        return -1;
    }
    rt_thread_startup(&s_thread);
    rt_kprintf("[RDSS] ready baud=%u cnr>%d\n",
               (unsigned)RDSS_UART_BAUD, RDSS_CNR_MIN);
    return 0;
}

static int app_rdss_init(void)
{
    return rdss_init();
}
INIT_APP_EXPORT(app_rdss_init);

#else /* !USE_RDSS */

int rdss_init(void) { return 0; }
int rdss_start_send(const uint8_t *p, uint16_t l) { (void)p; (void)l; return -RT_ERROR; }
int rdss_ensure_card(uint32_t t) { (void)t; return -RT_ERROR; }
uint32_t rdss_get_card_id(void) { return 0; }
int rdss_passthru_enter(void) { return -RT_ERROR; }
int rdss_passthru_exit(void) { return -RT_ERROR; }
int rdss_passthru_active(void) { return 0; }
int rdss_passthru_write(const uint8_t *d, uint32_t l) { (void)d; (void)l; return -RT_ERROR; }
void rdss_on_bat_protect(void) {}
struct rt_messagequeue *rdss_result_mq(void) { return RT_NULL; }

#endif /* USE_RDSS */
