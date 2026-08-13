/**
 * @file cfg.h
 * @brief 运行时产品配置（RAM；JSON cfg.*；Flash 后补）
 */
#ifndef __CFG_H__
#define __CFG_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 默认收信卡号（MBA01 CENTER_CARDID） */
#ifndef CFG_DEFAULT_RECV_ID
#define CFG_DEFAULT_RECV_ID     13500001u
#endif
/** 默认设备 ID stub（MBA01 ORIGINAL_ID） */
#ifndef CFG_DEFAULT_DEVICE_ID
#define CFG_DEFAULT_DEVICE_ID   1325000001u
#endif

int cfg_init(void);

uint32_t cfg_get_recv_id(void);
void     cfg_set_recv_id(uint32_t id);

/** 1=有效波束后开 PA；默认 0 */
uint8_t  cfg_get_pa_enable(void);
void     cfg_set_pa_enable(uint8_t en);

uint32_t cfg_get_device_id(void);
void     cfg_set_device_id(uint32_t id);

/** 拼 JSON 片段："recv_id":..,"pa_enable":..,"device_id":.. */
int cfg_to_json(char *buf, int buflen);

#ifdef __cplusplus
}
#endif

#endif /* __CFG_H__ */
