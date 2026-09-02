/**
 * @file cli.c
 * @brief CDC 下行拼行 + JSON 处理线程
 */
#include "cli.h"
#include "cli_json.h"
#include "cdc_io.h"
#include "product_config.h"
#include "mode.h"
#include "stream.h"
#if USE_GNSS
#include "gnss.h"
#endif
#if USE_RDSS
#include "rdss.h"
#endif
#if USE_BLE
#include "ble.h"
#endif

#include <rthw.h>
#include <rtthread.h>
#include <string.h>
#include <ulog.h>

static int line_is_json_cmd(const char *line)
{
    while ((line[0] == ' ') || (line[0] == '\t'))
    {
        line++;
    }
    return (line[0] == '{') ? 1 : 0;
}

static void passthru_downlink(const uint8_t *data, uint32_t len)
{
    uint8_t flags = mode_passthru_flags_get();

    if ((data == RT_NULL) || (len == 0u) || (flags == 0u))
    {
        return;
    }
#if USE_GNSS
    if (flags & MODE_PT_GNSS)
    {
        (void)gnss_passthru_write(data, len);
    }
#endif
#if USE_RDSS
    if (flags & MODE_PT_RDSS)
    {
        (void)rdss_passthru_write(data, len);
    }
#endif
}

typedef struct
{
    uint8_t ch;
    uint8_t b;
} cli_rx_t;

static struct rt_semaphore s_rx_notice;
static cli_rx_t s_rx_rb[CLI_RX_RB_SIZE];
static rt_uint16_t s_rx_head;
static rt_uint16_t s_rx_tail;
static char s_line[CLI_LINE_MAX];
static char s_rsp[CLI_RSP_MAX];
static struct rt_thread s_cli_thread;
static rt_uint8_t s_cli_stack[CLI_THREAD_STACK];
static cli_ch_t s_line_ch = CLI_CH_USB;

static void cli_rb_put(cli_ch_t ch, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    rt_base_t level;

    if ((ch == CLI_CH_NONE) || (data == RT_NULL) || (len == 0))
    {
        return;
    }
    level = rt_hw_interrupt_disable();
    for (i = 0; i < len; i++)
    {
        rt_uint16_t next = (rt_uint16_t)((s_rx_head + 1) % CLI_RX_RB_SIZE);
        if (next == s_rx_tail)
        {
            break;
        }
        s_rx_rb[s_rx_head].ch = (uint8_t)ch;
        s_rx_rb[s_rx_head].b = data[i];
        s_rx_head = next;
    }
    rt_hw_interrupt_enable(level);
}

static int cli_rb_get(cli_rx_t *out)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    if (s_rx_tail == s_rx_head)
    {
        rt_hw_interrupt_enable(level);
        return 0;
    }
    *out = s_rx_rb[s_rx_tail];
    s_rx_tail = (rt_uint16_t)((s_rx_tail + 1) % CLI_RX_RB_SIZE);
    rt_hw_interrupt_enable(level);
    return 1;
}

void cli_feed(cli_ch_t ch, const uint8_t *data, uint32_t len)
{
    cli_rb_put(ch, data, len);
    rt_sem_release(&s_rx_notice);
}

cli_ch_t cli_current_ch(void)
{
    return s_line_ch;
}

int cli_write(cli_ch_t ch, const uint8_t *data, uint32_t len)
{
    if ((data == RT_NULL) || (len == 0))
    {
        return 0;
    }
    if (ch == CLI_CH_USB)
    {
#if USE_USB_CDC
        return (int)cdc_acm_write(data, len);
#else
        return 0;
#endif
    }
    if (ch == CLI_CH_BLE)
    {
#if USE_BLE
        return ble_write(data, len);
#else
        return 0;
#endif
    }
    return 0;
}

static void cli_on_cdc_rx(const uint8_t *data, uint32_t len)
{
    cli_feed(CLI_CH_USB, data, len);
}

static void cli_thread_entry(void *param)
{
    int line_len = 0;

    (void)param;

    while (1)
    {
        cli_rx_t rx;

        if (rt_sem_take(&s_rx_notice, RT_WAITING_FOREVER) != RT_EOK)
        {
            continue;
        }

        while (cli_rb_get(&rx))
        {
            if ((rx.b == '\r') || (rx.b == '\n'))
            {
                if (line_len > 0)
                {
                    s_line[line_len] = '\0';
                    if ((mode_passthru_flags_get() != 0u) &&
                        (stream_passthru_ch() == s_line_ch) &&
                        !line_is_json_cmd(s_line))
                    {
                        static const uint8_t crlf[2] = {'\r', '\n'};

                        passthru_downlink((const uint8_t *)s_line, (uint32_t)line_len);
                        passthru_downlink(crlf, 2u);
                    }
                    else if (line_is_json_cmd(s_line))
                    {
                        int n = cli_json_handle_line(s_line, s_rsp, (int)sizeof(s_rsp));
                        if (n > 0)
                        {
                            (void)cli_write(s_line_ch, (const uint8_t *)s_rsp, (uint32_t)n);
                        }
                    }
                    line_len = 0;
                }
                continue;
            }

            if (line_len < (CLI_LINE_MAX - 1))
            {
                if (line_len == 0)
                {
                    s_line_ch = (cli_ch_t)rx.ch;
                }
                s_line[line_len++] = (char)rx.b;
            }
            else
            {
                line_len = 0;
            }
        }
    }
}

int cli_init(void)
{
    rt_err_t err;

    s_rx_head = 0;
    s_rx_tail = 0;
    rt_sem_init(&s_rx_notice, "cli_rx", 0, RT_IPC_FLAG_FIFO);
    cdc_acm_set_rx_callback(cli_on_cdc_rx);

    err = rt_thread_init(&s_cli_thread,
                         "cli",
                         cli_thread_entry,
                         RT_NULL,
                         s_cli_stack,
                         sizeof(s_cli_stack),
                         CLI_THREAD_PRIO,
                         20);
    if (err != RT_EOK)
    {
        LOG_E("cli thread init failed");
        return -1;
    }
    rt_thread_startup(&s_cli_thread);
    LOG_I("cli ready (JSON over USB/BLE)");
    return 0;
}

#if USE_CLI
/** APP 级：依赖 CDC(DEVICE) 已完成 */
static int app_cli_init(void)
{
    return cli_init();
}
INIT_APP_EXPORT(app_cli_init);
#endif
