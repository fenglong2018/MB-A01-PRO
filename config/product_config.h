/**
 * @file product_config.h
 * @brief 产品板型与功能开关（集中配置入口）
 *
 * 调试用法：
 * - CLI JSON：services/cli/README.md（需 USE_CLI）
 * - stream 透传：services/stream/README.md（USE_GNSS / USE_RDSS）
 */
#ifndef __PRODUCT_CONFIG_H__
#define __PRODUCT_CONFIG_H__

/* -------------------------------------------------------------------------- */
/* 板型选择                                                                   */
/* -------------------------------------------------------------------------- */
#define UNDECLARE_BOARD     0
#define N32WB452_EVB_BOARD  1   /* 评估板 / 当前工程默认板 */
#define USE_TARGET_BOARD    N32WB452_EVB_BOARD

#if (USE_TARGET_BOARD == N32WB452_EVB_BOARD)
/* 评估板默认功能 */
#define USE_USB_CDC         1   /* services/usb */
#define USE_KEY_IRQ         1   /* board EXTI，由 BSP/key 调用 */
#define USE_LED             1   /* BSP/led */
#define USE_KEY             1   /* BSP/key */
#define USE_ADC_BAT         1   /* BSP/adc 电池电压 */
#define USE_RDSS            1   /* BSP/rdss */
#define USE_GNSS            1   /* BSP/gnss */
#define USE_RTC             1   /* BSP/rtc：LSE 32768 + 日历，可被 GNSS/RDSS 校准 */
#define USE_PM              1   /* BSP/pm：idle hook → STOP0 浅睡 */
#define USE_IWDG            1   /* BSP/iwdg：非关机态开，超时≈26s */
#define USE_CLI             1   /* services/cli：JSON 控制面 */
#define USE_BLE             1   /* BSP/ble：插 USB 开、拔 USB 关；Nations slave 栈 */
#define USE_JSON_FILE       0
#define USE_MODBUS          0
#else
#define USE_USB_CDC         0
#define USE_KEY_IRQ         0
#define USE_LED             0
#define USE_KEY             0
#define USE_ADC_BAT         0
#define USE_RDSS            0
#define USE_GNSS            0
#define USE_RTC             0
#define USE_PM              0
#define USE_IWDG            0
#define USE_CLI             0
#define USE_BLE             0
#define USE_JSON_FILE       0
#define USE_MODBUS          0
#endif

#endif /* __PRODUCT_CONFIG_H__ */
