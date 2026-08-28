/**
 * @file usb_hw.h
 * @brief 板级 USB 声明（实现见 usb_hw.c / nations/hw_config.h）
 */
#ifndef __USB_HW_H__
#define __USB_HW_H__

#include "hw_config.h"
#include <stdint.h>

/*
 * 枚举诊断计数器。断点会打断 USB 实时时序、本身就会让枚举失败，
 * 这两个量可以让 J-Link 在不停机的情况下判断 USB 核到底有没有在收总线：
 *  - g_usb_irq_cnt == 0：USB 中断一次没进，48M 时钟或 NVIC 有问题
 *  - g_usb_irq_cnt > 0 但 g_usb_reset_cnt == 0：收得到中断但认不出主机 RESET
 *  - 两个都在涨却仍枚举失败：时钟没问题，要往描述符/EP0 查
 */
extern volatile uint32_t g_usb_irq_cnt;
extern volatile uint32_t g_usb_reset_cnt;

/*
 * EP0 定位用。卡在 ATTACHED 时这三个量把控制传输切成三段：
 *  - g_usb_ctr_cnt == 0：CTR 中断没来，SETUP 包压根没收到（PMA/BTABLE 或 EP0 配置）
 *  - ctr > 0 但 g_usb_desc_cnt == 0：收到了 SETUP 但没解析成取设备描述符
 *  - desc_cnt > 0：我们答了但主机不认，问题在描述符内容或 EP0 发送通路
 * g_usb_last_req 打包最后一次 SETUP：[31:24]bmRequestType [23:16]bRequest [15:0]wValue
 */
extern volatile uint32_t g_usb_ctr_cnt;
extern volatile uint32_t g_usb_desc_cnt;
extern volatile uint32_t g_usb_last_req;

/*
 * 分辨中断到底来自哪：SOF 多说明收包正常，ERR 多说明 D+/D- 上每个包都收错，
 * 后者是模拟侧问题（串阻/上拉/走线），改固件没用。
 */
extern volatile uint32_t g_usb_sof_cnt;
extern volatile uint32_t g_usb_err_cnt;

#endif /* __USB_HW_H__ */
