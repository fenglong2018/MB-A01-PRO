/**
 * @file cdc_acm.h
 * @brief USB CDC ACM 端点 / 设备描述符配置与接口
 */
#ifndef __CDC_ACM_H__
#define __CDC_ACM_H__

#include <stdint.h>
#include "usb_cdc.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!< endpoint address */
#ifndef CDC_IN_EP
#define CDC_IN_EP  0x81
#endif
#ifndef CDC_OUT_EP
#define CDC_OUT_EP 0x02
#endif
#ifndef CDC_INT_EP
#define CDC_INT_EP 0x83
#endif

#ifndef USBD_VID
#define USBD_VID           0x19F5
#endif
#ifndef USBD_PID
#define USBD_PID           0x5245
#endif
#ifndef USBD_MAX_POWER
#define USBD_MAX_POWER     100
#endif
#ifndef USBD_LANGID_STRING
#define USBD_LANGID_STRING 1033
#endif

/*!< config descriptor size */
#ifndef USB_CONFIG_SIZE
#define USB_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN)
#endif

void cdc_acm_init(void);
void cdc_acm_data_send_with_dtr_test(void);

/** 主机打开虚拟串口时通常置位 DTR；ulog CDC 后端用此判断可否发送 */
uint8_t cdc_acm_is_dtr_enable(void);

/** 经 CDC IN 端点发送数据；未连接/失败时返回已发送字节数（可能为 0） */
uint32_t cdc_acm_write(const uint8_t *data, uint32_t len);

/** CDC OUT 收包回调（在 USB 中断上下文，只允许轻量投递） */
void cdc_acm_set_rx_callback(void (*cb)(const uint8_t *data, uint32_t len));

#ifdef __cplusplus
}
#endif

#endif /* __CDC_ACM_H__ */
