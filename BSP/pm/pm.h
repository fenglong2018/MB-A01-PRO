/**
 * @file pm.h
 * @brief MCU 浅睡（STOP0）+ idle hook；与业务解耦，用 lock 禁止入睡
 *
 * 深睡 STOP2 / 假关机见 docs/low_power_stop2.md（未在本模块实施）。
 */
#ifndef __BSP_PM_H__
#define __BSP_PM_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 禁止进入 STOP0（可嵌套）。
 * 用法：会话/透传开始 pm_lock()，结束 pm_unlock()。
 * pm 不依赖 mode/gnss，由上层自行配对，避免循环包含。
 */
void pm_lock(void);
void pm_unlock(void);

/** 当前禁止计数；0=允许 tidle 进 STOP0 */
uint32_t pm_lock_count(void);

/** 向 RT-Thread idle 注册 STOP0 钩子；INIT_APP 已自动调用，幂等 */
int pm_idle_hook_install(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_PM_H__ */
