/**
 * @file cli_ch.h
 * @brief CLI 传输通道：USB CDC / BLE。应答与透传按通道回发起方。
 */
#ifndef __CLI_CH_H__
#define __CLI_CH_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    CLI_CH_NONE = 0,
    CLI_CH_USB  = 1,
    CLI_CH_BLE  = 2,
} cli_ch_t;

#ifdef __cplusplus
}
#endif

#endif /* __CLI_CH_H__ */
