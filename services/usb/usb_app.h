/**
 * @file usb_app.h
 * @brief USB 应用层接口（初始化已走 RT-Thread INIT_*_EXPORT）
 */
#ifndef __USB_APP_H__
#define __USB_APP_H__

#ifdef __cplusplus
extern "C" {
#endif

/** 兼容旧接口；当前为空（CDC/ulog/cli 已自动注册） */
void usb_app_init(void);

/** 周期轮询占位（当前无操作） */
void usb_app_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* __USB_APP_H__ */
