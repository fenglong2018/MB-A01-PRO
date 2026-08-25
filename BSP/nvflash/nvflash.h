/**
 * @file nvflash.h
 * @brief 片内 Flash 双槽原始块（不含业务字段）
 *
 * 末尾 2×2KB。整块 CRC 失败则丢弃该槽。不依赖 cfg/mode。
 */
#ifndef __NVFLASH_H__
#define __NVFLASH_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NVFLASH_SLOT_A
#define NVFLASH_SLOT_A          0x0807F000u
#endif
#ifndef NVFLASH_SLOT_B
#define NVFLASH_SLOT_B          0x0807F800u
#endif
#ifndef NVFLASH_PAGE_SIZE
#define NVFLASH_PAGE_SIZE       2048u
#endif
#ifndef NVFLASH_PAYLOAD_MAX
#define NVFLASH_PAYLOAD_MAX     512u
#endif

int nvflash_init(void);

/**
 * 读 seq 更大且 CRC 通过的槽。成功把 payload 拷到 out，返回长度；失败 -1。
 */
int nvflash_load(void *out, uint16_t out_max);

/** 写入空闲槽（seq+1）。payload 与已存相同则跳过。0=ok */
int nvflash_save(const void *payload, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __NVFLASH_H__ */
