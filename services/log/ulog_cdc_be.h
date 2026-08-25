/**
 * @file ulog_cdc_be.h
 * @brief ulog CDC 后端注册接口与参数宏
 */
#ifndef __ULOG_CDC_BE_H__
#define __ULOG_CDC_BE_H__

#ifdef __cplusplus
extern "C" {
#endif

/** 等待主机打开串口（DTR）的最长时间（ms） */
#define ULOG_CDC_DTR_WAIT_MS    3000
/** DTR 轮询间隔（ms） */
#define ULOG_CDC_DTR_POLL_MS    10
/** 进入 PASSTHRU 时默认静音 CDC ulog，避免与 NMEA/$BD 交错 */
#define ULOG_CDC_PASSTHRU_MUTE_DEFAULT  1

int ulog_cdc_backend_init(void);

/** MODE 进入/退出 PASSTHRU 时调用；1=占用透传（配合 mute 策略丢日志） */
void ulog_cdc_passthru_hold(int hold);
int ulog_cdc_passthru_held(void);

/**
 * 透传期间是否静音 CDC 日志。1=静音（默认），0=仍打 ulog。
 * 仅 RAM，不写 cfg/Flash；掉电恢复默认。
 */
void ulog_cdc_set_passthru_mute(int mute);
int ulog_cdc_get_passthru_mute(void);

#ifdef __cplusplus
}
#endif

#endif /* __ULOG_CDC_BE_H__ */
