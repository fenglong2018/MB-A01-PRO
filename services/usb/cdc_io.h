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
uint32_t cdc_acm_write(const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* __CDC_IO_H__ */
