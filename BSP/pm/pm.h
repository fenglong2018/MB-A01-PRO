/**
 * @file pm.h
 * @brief MCU 浅睡（STOP0）+ idle hook；与业务解耦，用 lock 禁止入睡
 *
 * 深睡 STOP2：pm_stop2_enter()（真关机）。假关机 10s 心跳见 RTC WakeUp。
 */
#ifndef __BSP_PM_H__
#define __BSP_PM_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 禁止进入 STOP0（可嵌套）。
 * MODE 非 FAKE_OFF 时会持有一层；会话发信、nvflash、KEY 滤波再叠。
 * pm 不依赖 mode/gnss，由上层自行配对，避免循环包含。
 *
 * USB 不走 lock：idle 读 PA7，或 CDC 已 start（防脚抖进 STOP0 掐 48M）。
 * STOP0 会停 PLL，USB 48M 无法单独保留。
 */
void pm_lock(void);
void pm_unlock(void);

/** 当前禁止计数；0=允许 tidle 进 STOP0 */
uint32_t pm_lock_count(void);

/** 向 RT-Thread idle 注册 STOP0 钩子；INIT_APP 已自动调用，幂等 */
int pm_idle_hook_install(void);

/**
 * 真关机 STOP2：普通 SRAM 丢失。切到 R-SRAM 栈再 WFI，醒后软件复位。
 * 调用前须已写 BKP、关 10s 心跳、iwdg_stop2_quiet()。不返回。
 */
void pm_stop2_enter(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_PM_H__ */
