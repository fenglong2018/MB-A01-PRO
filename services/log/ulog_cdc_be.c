/**
 * @file ulog_cdc_be.c
 * @brief ulog 自定义后端：异步线程取出日志后，经 USB CDC 发到 PC 虚拟串口
 *
 * 注意：不要包含 cdc_acm.h（会拉入 usb_cdc.h 的 __PACKED，与工程其它头冲突）。
 */
#include <rtthread.h>
#include <stdint.h>
#include <ulog.h>
#include "ulog_cdc_be.h"
#include "cdc_io.h"
#include "product_config.h"

static struct ulog_backend s_cdc_be;

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

    while (!cdc_acm_is_dtr_enable() && (wait_ms < ULOG_CDC_DTR_WAIT_MS))
    {
        rt_thread_mdelay(ULOG_CDC_DTR_POLL_MS);
        wait_ms += ULOG_CDC_DTR_POLL_MS;
    }

    if (!cdc_acm_is_dtr_enable())
    {
        return;
    }

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
