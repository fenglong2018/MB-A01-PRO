/**
 * @file stream_ports.h
 * @brief 逻辑数据通道定义（与物理 USB 口数量无关）
 *
 * 当前：单 CDC，控制/LOG 走 CLI 通道；GNSS/RDSS 仅预留。
 * 以后复合多 CDC 时，只改 stream 后端绑定，不改通道语义。
 */
#ifndef __STREAM_PORTS_H__
#define __STREAM_PORTS_H__

#ifdef __cplusplus
extern "C" {
#endif

#define STREAM_NAME_CLI     "cli"
#define STREAM_NAME_GNSS    "gnss"
#define STREAM_NAME_RDSS    "rdss"

/** 逻辑通道号（帧复用 / 多 CDC 映射用） */
#define STREAM_CH_CLI       0
#define STREAM_CH_GNSS      1
#define STREAM_CH_RDSS      2

#ifdef __cplusplus
}
#endif

#endif /* __STREAM_PORTS_H__ */
