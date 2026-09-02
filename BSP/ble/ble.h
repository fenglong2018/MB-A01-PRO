/**
 * @file ble.h
 * @brief 产品 BLE：插 USB 开、拔 USB 关。数据面走同一套 JSON CLI。
 *
 * 链入 Nations IAR host.a / n32wb452_ble.a（对照 examples/BLE/slave）。
 * 插 USB 开广播，拔 USB 关射频；JSON 走同一套 CLI。
 */
#ifndef __BSP_BLE_H__
#define __BSP_BLE_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BLE_THREAD_STACK
#define BLE_THREAD_STACK    3072
#endif
#ifndef BLE_THREAD_PRIO
#define BLE_THREAD_PRIO     18
#endif
#ifndef BLE_NAME_PREFIX
#define BLE_NAME_PREFIX     "MBA01"
#endif

void ble_start(void);
void ble_stop(void);
int  ble_is_on(void);
int  ble_is_connected(void);
/** 已连接则 notify；未开栈时返回 0 */
int  ble_write(const uint8_t *data, uint32_t len);
void ble_name(char *buf, int bufsz);
int  ble_stack_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_BLE_H__ */
