/**
 * @file msg_pack.h
 * @brief MBA01 风格短报文组包（缺字段先 stub）
 */
#ifndef __MSG_PACK_H__
#define __MSG_PACK_H__

#include <stdint.h>
#include "gnss.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MSG_LOCADATA_LEN        34
#define MSG_LOCADATA_HEADLEN    22
#define MSG_PACK_MAX            (MSG_LOCADATA_HEADLEN + MSG_LOCADATA_LEN)

/**
 * 组一包上报：头(设备ID+首次定位时间stub+电量) + 单点定位体。
 * alarm_mode：非 0 → 'A'，否则 'N'。
 * 成功返回总长度，失败返回 0。
 */
uint16_t msg_pack_loca_up(uint8_t *out, uint16_t out_max,
                          const gnss_fix_t *fix, uint8_t alarm_mode);

#ifdef __cplusplus
}
#endif

#endif /* __MSG_PACK_H__ */
