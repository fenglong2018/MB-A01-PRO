/**
 * @file gnss.h
 * @brief GNSS（ATGM336H）：正常一次定位 / 透传；规则见 README.md
 */
#ifndef __GNSS_H__
#define __GNSS_H__

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GNSS_THREAD_STACK
#define GNSS_THREAD_STACK       1536
#endif
#ifndef GNSS_THREAD_PRIO
#define GNSS_THREAD_PRIO        22
#endif

#ifndef GNSS_UART_BAUD
#define GNSS_UART_BAUD          115200
#endif
#ifndef GNSS_FIX_TIMEOUT_MS
#define GNSS_FIX_TIMEOUT_MS     30000
#endif
#ifndef GNSS_PWR_STABLE_MS
#define GNSS_PWR_STABLE_MS      5
#endif

#ifndef GNSS_CMD_MQ_DEPTH
#define GNSS_CMD_MQ_DEPTH       8
#endif
#ifndef GNSS_RESULT_MQ_DEPTH
#define GNSS_RESULT_MQ_DEPTH    4
#endif

/** 完成原因 */
#define GNSS_RESULT_OK          0u
#define GNSS_RESULT_TIMEOUT     1u
#define GNSS_RESULT_ABORTED     2u  /* 被透传/关电打断 */

typedef struct
{
    int32_t  lat_e7;     /* 纬度 * 1e7 */
    int32_t  lon_e7;     /* 经度 * 1e7 */
    int16_t  alt_dm;     /* 海拔 0.1m；无效 0x7FFF */
    uint8_t  quality;    /* GGA quality */
    uint8_t  satellites;
    uint8_t  valid;      /* 1=有有效缓存 */
    char     utc[11];    /* GGA "hhmmss.ss" 或截断 */
    uint32_t unix_sec;   /* RMC 日期+时刻合成的 UTC Unix；无合法 RMC 则为 0 */
} gnss_fix_t;

typedef struct
{
    uint8_t     reason;  /* GNSS_RESULT_* */
    uint8_t     ok;      /* 1=本次得到有效定位 */
    gnss_fix_t fix;
} gnss_msg_t;

int gnss_init(void);

/** 请求一次正常定位（非阻塞）；透传中返回 -EBUSY */
int gnss_start_fix(void);

int gnss_passthru_enter(void);
int gnss_passthru_exit(void);
int gnss_passthru_active(void);

/** 透传：主机 → 模块 */
int gnss_passthru_write(const uint8_t *data, uint32_t len);

/**
 * MODE 关断 / 保护：立刻停定位/透传并关电。
 */
void gnss_on_bat_protect(void);

/** 最近一次有效定位（可能是更早会话留下的） */
int gnss_get_fix(gnss_fix_t *out);

/** 结果队列（session/RDSS 接收）；勿销毁 */
struct rt_messagequeue *gnss_result_mq(void);

#ifdef __cplusplus
}
#endif

#endif /* __GNSS_H__ */
