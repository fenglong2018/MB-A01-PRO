/**
 * @file rdss.h
 * @brief RDSS（TD3050）：等波束 / 可选 PA / CCTCQ 发信 / 透传 / 本机卡号
 */
#ifndef __RDSS_H__
#define __RDSS_H__

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RDSS_THREAD_STACK
#define RDSS_THREAD_STACK       2048
#endif
#ifndef RDSS_THREAD_PRIO
#define RDSS_THREAD_PRIO        22
#endif
#ifndef RDSS_UART_BAUD
#define RDSS_UART_BAUD          115200
#endif
#ifndef RDSS_BEAM_TIMEOUT_MS
#define RDSS_BEAM_TIMEOUT_MS    30000
#endif
#ifndef RDSS_FKI_TIMEOUT_MS
#define RDSS_FKI_TIMEOUT_MS     15000
#endif
#ifndef RDSS_CARD_TIMEOUT_MS
#define RDSS_CARD_TIMEOUT_MS    5000
#endif
#ifndef RDSS_PWR_STABLE_MS
#define RDSS_PWR_STABLE_MS      50
#endif
#ifndef RDSS_CNR_MIN
#define RDSS_CNR_MIN            40  /* S2C_d > 40 */
#endif
#ifndef RDSS_PWI_TIME_MIN
#define RDSS_PWI_TIME_MIN       20  /* PWI 时间字段 > 20 */
#endif
#ifndef RDSS_TX_PAYLOAD_MAX
#define RDSS_TX_PAYLOAD_MAX     256
#endif

#define RDSS_RESULT_OK          0u
#define RDSS_RESULT_BEAM_TO     1u
#define RDSS_RESULT_FKI_FAIL    2u
#define RDSS_RESULT_ABORTED     3u
#define RDSS_RESULT_BUSY        4u
#define RDSS_RESULT_PARAM       5u
#define RDSS_RESULT_CARD_TO     6u

typedef struct
{
    uint8_t reason; /* RDSS_RESULT_* */
    uint8_t ok;
} rdss_msg_t;

int rdss_init(void);

/**
 * 请求一次发信（非阻塞）：上电→等有效波束→可选开 PA→CCTCQ→等 FKI→关电。
 * payload 为业务明文（内部转 HEX 代码）；收信地址取 cfg_get_recv_id()。
 * 上电后发 $CCICR，解析 $BDICP 字段1 为本机卡号。
 */
int rdss_start_send(const uint8_t *payload, uint16_t len);

/**
 * 阻塞查询本机卡号（TD3050：$CCICR → $BDICP 字段1）。
 * 已缓存则立即返回；成功写入内部缓存并 cfg_note_bd_card（号变才落 Flash）。
 */
int rdss_ensure_card(uint32_t timeout_ms);

/** 已缓存的本机卡号；0=尚未从 RDSS 取得 */
uint32_t rdss_get_card_id(void);

int rdss_passthru_enter(void);
int rdss_passthru_exit(void);
int rdss_passthru_active(void);
int rdss_passthru_write(const uint8_t *data, uint32_t len);

void rdss_on_bat_protect(void);

struct rt_messagequeue *rdss_result_mq(void);

#ifdef __cplusplus
}
#endif

#endif /* __RDSS_H__ */
