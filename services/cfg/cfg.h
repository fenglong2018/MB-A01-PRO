/**
 * @file cfg.h
 * @brief 运行时配置：RAM + Flash 身份（nv 线程）。MODE/告警不在此。
 */
#ifndef __CFG_H__
#define __CFG_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CFG_DEFAULT_RECV_ID
#define CFG_DEFAULT_RECV_ID     13500001u
#endif
#ifndef CFG_DEFAULT_DEVICE_ID
#define CFG_DEFAULT_DEVICE_ID   1325000001u
#endif
#ifndef CFG_DEFAULT_CHARGE_OFFSET_MV
#define CFG_DEFAULT_CHARGE_OFFSET_MV    0u
#endif
#ifndef CFG_CHARGE_OFFSET_MV_MAX
#define CFG_CHARGE_OFFSET_MV_MAX        500u
#endif
/** ADC 满量程校准（mV），当 VDDA；不采内部 1.2V */
#ifndef CFG_DEFAULT_ADC_VDDA_MV
#define CFG_DEFAULT_ADC_VDDA_MV         3300u
#endif
#ifndef CFG_ADC_VDDA_MV_MIN
#define CFG_ADC_VDDA_MV_MIN             2500u
#endif
#ifndef CFG_ADC_VDDA_MV_MAX
#define CFG_ADC_VDDA_MV_MAX             4000u
#endif
#ifndef CFG_BD_SLOTS
#define CFG_BD_SLOTS            4u
#endif
#ifndef CFG_ICCID_SLOTS
#define CFG_ICCID_SLOTS         4u
#endif
#ifndef CFG_ICCID_LEN
#define CFG_ICCID_LEN           21u
#endif
#ifndef CFG_VER_LEN
#define CFG_VER_LEN             16u
#endif
#ifndef CFG_SW_VER
#define CFG_SW_VER              "v0.2"
#endif
#ifndef CFG_HW_VER
#define CFG_HW_VER              ""
#endif

int cfg_init(void);

uint32_t cfg_get_recv_id(void);
void     cfg_set_recv_id(uint32_t id);

uint8_t  cfg_get_pa_enable(void);
void     cfg_set_pa_enable(uint8_t en);

uint32_t cfg_get_device_id(void);
/** 设备编号；cfg.set / 出厂。不被 $BDICP 调用 */
void     cfg_set_device_id(uint32_t id);

uint16_t cfg_get_charge_offset_mv(void);
void     cfg_set_charge_offset_mv(uint16_t mv);

uint16_t cfg_get_adc_vdda_mv(void);
void     cfg_set_adc_vdda_mv(uint16_t mv);

/** $BDICP：只改当前北斗卡 RAM；号变了才进 Flash 历史槽 */
void     cfg_note_bd_card(uint32_t id);
uint32_t cfg_get_bd_card(void);

uint32_t cfg_get_first_fix_unix(void);
void     cfg_set_first_fix_unix(uint32_t unix_sec);

const char *cfg_get_hw_ver(void);
const char *cfg_get_sw_ver(void);
uint32_t    cfg_get_upgrade_unix(void);
/** 出厂写硬件版本；空串忽略。软件版本 / 升级 Unix 不走 cfg.set */
void        cfg_set_hw_ver(const char *ver);

int cfg_to_json(char *buf, int buflen);

#ifdef __cplusplus
}
#endif

#endif /* __CFG_H__ */
