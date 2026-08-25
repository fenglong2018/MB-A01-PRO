/**
 * @file ulog_cdc_be.c
 * @brief ulog 自定义后端：异步线程取出日志后，经 USB CDC 发到 PC 虚拟串口
 *
 * 注意：不要包含 cdc_acm.h（会拉入 usb_cdc.h 的 __PACKED，与工程其它头冲突）。
 */
#include <rtthread.h>
#include <rthw.h>
#include <stdint.h>
#include <ulog.h>
#include "ulog_cdc_be.h"
#include "cdc_io.h"
#include "product_config.h"

static struct ulog_backend s_cdc_be;
static volatile uint8_t s_pt_hold;
static volatile uint8_t s_pt_mute = (uint8_t)ULOG_CDC_PASSTHRU_MUTE_DEFAULT;
static volatile uint8_t s_banner_done;

/** 仅线程上下文（ulog_async）。USB ISR 里不能打、也不能启定时器。 */
static void console_banner_try_print(void)
{
    static char buf[160];
    rt_base_t level;
    int n;

    if (!cdc_acm_is_dtr_enable())
    {
        return;
    }

    level = rt_hw_interrupt_disable();
    if (s_banner_done)
    {
        rt_hw_interrupt_enable(level);
        return;
    }
    s_banner_done = 1u;
    rt_hw_interrupt_enable(level);

    n = rt_snprintf(buf, sizeof(buf),
                    "\r\n \\ | /\r\n"
                    "- RT -     Thread Operating System\r\n"
                    " / | \\     %d.%d.%d build %s\r\n"
                    " 2006 - 2019 Copyright by rt-thread team\r\n",
                    (int)RT_VERSION, (int)RT_SUBVERSION, (int)RT_REVISION,
                    __DATE__);
    if (n <= 0)
    {
        return;
    }
    if (n >= (int)sizeof(buf))
    {
        n = (int)sizeof(buf) - 1;
    }
    (void)cdc_acm_write((const uint8_t *)buf, (uint32_t)n);
}

void ulog_cdc_passthru_hold(int hold)
{
    s_pt_hold = hold ? 1u : 0u;
}

int ulog_cdc_passthru_held(void)
{
    return s_pt_hold ? 1 : 0;
}

void ulog_cdc_set_passthru_mute(int mute)
{
    s_pt_mute = mute ? 1u : 0u;
}

int ulog_cdc_get_passthru_mute(void)
{
    return s_pt_mute ? 1 : 0;
}

/**
 * 在 ulog_async 线程上下文中调用。
 * PC 未打开串口（DTR=0）时短暂等待，尽量保住环形缓冲里的早期日志；
 * 超时仍未就绪则丢弃该行，避免永久卡住异步线程。
 */
static void ulog_cdc_backend_output(struct ulog_backend *backend,
                                    rt_uint32_t level,
                                    const char *tag,
                                    rt_bool_t is_raw,
                                    const char *log,
                                    size_t len)
{
    int wait_ms = 0;

    (void)backend;
    (void)level;
    (void)tag;
    (void)is_raw;

    if ((log == RT_NULL) || (len == 0))
    {
        return;
    }

    /* 透传占用同一 CDC：默认丢弃 ulog，避免插进 NMEA 行中间 */
    if (s_pt_hold && s_pt_mute)
    {
        return;
    }

    while (!cdc_acm_is_dtr_enable() && (wait_ms < ULOG_CDC_DTR_WAIT_MS))
    {
        rt_thread_mdelay(ULOG_CDC_DTR_POLL_MS);
        wait_ms += ULOG_CDC_DTR_POLL_MS;
    }

    if (!cdc_acm_is_dtr_enable())
    {
        return;
    }

    console_banner_try_print();
    (void)cdc_acm_write((const uint8_t *)log, (uint32_t)len);
}

int ulog_cdc_backend_init(void)
{
    ulog_init();

    rt_memset(&s_cdc_be, 0, sizeof(s_cdc_be));
    s_cdc_be.output = ulog_cdc_backend_output;
    ulog_backend_register(&s_cdc_be, "cdc", RT_FALSE);

    return 0;
}

#if USE_USB_CDC
/** COMPONENT 级：依赖 ulog(PREV) + CDC(DEVICE) */
static int app_ulog_cdc_be_init(void)
{
    return ulog_cdc_backend_init();
}
INIT_COMPONENT_EXPORT(app_ulog_cdc_be_init);
#endif
