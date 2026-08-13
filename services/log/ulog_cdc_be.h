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

int ulog_cdc_backend_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __ULOG_CDC_BE_H__ */
