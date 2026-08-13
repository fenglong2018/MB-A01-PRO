/**
 * @file pwr_plna.h
 * @brief MCU_EN_PLNA 共享电源轨（GNSS / RDSS 引用计数）
 */
#ifndef __PWR_PLNA_H__
#define __PWR_PLNA_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void pwr_plna_init(void);

/** 需要 PLNA 时调用；首次 acquire 拉高脚 */
void pwr_plna_acquire(void);

/** 用完 release；计数到 0 拉低脚 */
void pwr_plna_release(void);

uint8_t pwr_plna_refcount(void);

#ifdef __cplusplus
}
#endif

#endif /* __PWR_PLNA_H__ */
