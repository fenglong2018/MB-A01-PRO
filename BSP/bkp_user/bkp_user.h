/**
 * @file bkp_user.h
 * @brief 备份域用户区（DAT3 起）。DAT1/DAT2 留给 RTC，本模块不碰。
 *
 * 无业务语义：只存调用方给的字节 + CRC。不依赖 mode/cfg。
 */
#ifndef __BKP_USER_H__
#define __BKP_USER_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BKP_USER_BYTES_MAX
#define BKP_USER_BYTES_MAX      64u
#endif

int bkp_user_init(void);

/** 写入；附 CRC。len 过大返回 -1。0=ok */
int bkp_user_save(const void *data, uint16_t len);

/** CRC 通过则拷到 out，返回长度；失败 -1 */
int bkp_user_load(void *out, uint16_t out_max);

#ifdef __cplusplus
}
#endif

#endif /* __BKP_USER_H__ */
