/**
 * @file n32wb452_log_level.h
 * @brief Nations ble_log 宏；默认关掉，避免 printf 打进 CDC
 */
#ifndef __N32WB452_LOG_LEVEL_H__
#define __N32WB452_LOG_LEVEL_H__

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_NO_LOG  0
#define BLE_DBG     1
#define BLE_INFO    2
#define BLE_WARNING 3
#define BLE_ERROR   4
#define BLE_FAULT   5
#define BLE_LOG_LIMIT 6

#define ble_log(x, ...)

#ifdef __cplusplus
}
#endif

#endif /* __N32WB452_LOG_LEVEL_H__ */
