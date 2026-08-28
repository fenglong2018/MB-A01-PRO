/**
 * @file cdc_io.h
 * @brief CDC 收发薄接口（不包含 usb_cdc.h，避免 __PACKED 冲突）
 */
#ifndef __CDC_IO_H__
#define __CDC_IO_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*cdc_rx_cb_t)(const uint8_t *data, uint32_t len);

void cdc_acm_set_rx_callback(cdc_rx_cb_t cb);
uint8_t cdc_acm_is_dtr_enable(void);
/** 1=已 usb_cdc_start（DP 上拉/PHY 开着）；idle 禁 STOP0 用，不看 PA7 抖动 */
uint8_t usb_cdc_is_on(void);
uint8_t usb_cdc_is_configured(void);
void usb_cdc_start(void);
void usb_cdc_stop(void);
uint32_t cdc_acm_write(const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* __CDC_IO_H__ */
