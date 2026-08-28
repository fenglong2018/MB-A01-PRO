/**
 * @file cdc_acm.h
 * @brief USB CDC ACM 应用接口（Nations usbfs 栈）
 */
#ifndef __CDC_ACM_H__
#define __CDC_ACM_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void cdc_acm_init(void);
/** 仅 USB 插入确认后调用；未插线不拉 DP，避免关机 1.6mA */
void usb_cdc_start(void);
/** 拔线确认 / STOP2 前：关上拉、PHY、脚改模拟 */
void usb_cdc_stop(void);
/** 1=CDC 已启动（PHY/DP 上拉开着） */
uint8_t usb_cdc_is_on(void);
/** 1=主机已 SET_CONFIGURATION（或随后 suspend） */
uint8_t usb_cdc_is_configured(void);
void cdc_acm_data_send_with_dtr_test(void);

/** 主机打开虚拟串口时通常置位 DTR；ulog CDC 后端用此判断可否发送 */
uint8_t cdc_acm_is_dtr_enable(void);

/** 经 CDC IN 端点发送数据；未连接/失败时返回已发送字节数（可能为 0） */
uint32_t cdc_acm_write(const uint8_t *data, uint32_t len);

/** CDC OUT 收包回调（在 USB 中断上下文，只允许轻量投递） */
void cdc_acm_set_rx_callback(void (*cb)(const uint8_t *data, uint32_t len));

extern volatile uint8_t dtr_enable;

#ifdef __cplusplus
}
#endif

#endif /* __CDC_ACM_H__ */
