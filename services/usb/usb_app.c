/**
 * @file usb_app.c
 * @brief USB 应用层占位（初始化已改为 INIT_*_EXPORT 自动注册）
 *
 * 启动顺序见各模块文件末尾：
 * - DEVICE   app_usb_cdc_init       (cdc_acm.c)
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
