/**
 * @file usb_app.c
 * @brief USB 应用层占位（CDC 在 rt_hw_init 尽早 USB_Init，不再走 INIT_DEVICE_EXPORT）
 *
 * 启动顺序：
 * - BOARD    cdc_acm_init           (rt_hw_init：时钟 + USB_Init)
 * - COMPONENT app_ulog_cdc_be_init  (ulog_cdc_be.c)
 * - COMPONENT app_stream_init       (stream.c, USE_CLI)
 * - APP       app_cli_init          (cli.c, USE_CLI)
 */
#include "usb_app.h"

void usb_app_init(void)
{
    /* 保留接口兼容；实际由组件初始化表自动执行，勿在此重复拉起 */
}

void usb_app_poll(void)
{
}
