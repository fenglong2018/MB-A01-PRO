/**
 * @file rram.h
 * @brief 16KB Retention RAM 原始块（STOP2 保持；看门狗/POR 一般丢失）
 *
 * 链接到 0x20020000。本模块不解释内容，不依赖 mode/cfg。
 * STOP2 尚未启用，仅预留段与 CRC 存取。
 */
#ifndef __RRAM_H__
#define __RRAM_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RRAM_BLOB_MAX
#define RRAM_BLOB_MAX           256u
#endif

int rram_init(void);
int rram_save(const void *data, uint16_t len);
int rram_load(void *out, uint16_t out_max);

#ifdef __cplusplus
}
#endif

#endif /* __RRAM_H__ */
