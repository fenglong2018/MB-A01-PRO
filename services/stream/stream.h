/**
 * @file stream.h
 * @brief 数据流抽象：运行时开关 + 透传出口（GNSS/RDSS 预留）
 */
#ifndef __STREAM_H__
#define __STREAM_H__

#include <stdint.h>
#include "cli_ch.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STREAM_OK              0
#define STREAM_ERR_UNKNOWN    (-1)
#define STREAM_ERR_NOT_BUILT  (-2)
#define STREAM_ERR_DISABLED   (-3)
#define STREAM_ERR_NO_SINK    (-4)
#define STREAM_ERR_PARAM      (-5)

typedef int (*stream_sink_fn)(const uint8_t *data, uint32_t len);

int stream_init(void);

/** 运行时开关；未编进固件的流返回 STREAM_ERR_NOT_BUILT */
int stream_set_enable(const char *name, int enable);
int stream_set_enable_ch(const char *name, int enable, cli_ch_t ch);
/** 当前透传发起方；未开则为 CLI_CH_NONE */
cli_ch_t stream_passthru_ch(void);
void stream_passthru_reset(void);

int stream_is_enabled(const char *name);
int stream_is_built(const char *name);

/**
 * 驱动侧把收到的原始数据交给 stream（透传到主机 USB CDC）。
 * 无 sink 时返回 STREAM_ERR_NO_SINK。
 */
int stream_write(const char *name, const uint8_t *data, uint32_t len);

/** 默认在 stream_init 接到 cdc_acm_write；传 NULL 清除 */
int stream_set_sink(const char *name, stream_sink_fn sink);

/**
 * 填充 JSON 数组片段到 buf，例如：
 * [{"name":"gnss","ch":1,"built":0,"enable":0},...]
 * 返回写入长度，失败返回 <0
 */
int stream_list_json(char *buf, int buflen);

const char *stream_err_str(int err);

#ifdef __cplusplus
}
#endif

#endif /* __STREAM_H__ */
