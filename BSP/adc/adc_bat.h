/**
 * @file adc_bat.h
 * @brief 锂电池电压采集 → OCV 查表百分比 / 电量等级
 *
 * 链路：ADC码值 → Vbat(mV) → 101 档 percent(0~100) → level
 * 表数据在 adc_bat_soc.h（const 数组，非宏）；本头文件只放尺寸与门限宏。
 */
#ifndef __ADC_BAT_H__
#define __ADC_BAT_H__

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ADC_BAT_THREAD_STACK
#define ADC_BAT_THREAD_STACK    1024
#endif
#ifndef ADC_BAT_THREAD_PRIO
#define ADC_BAT_THREAD_PRIO     20
#endif
#ifndef ADC_BAT_POLL_MS
#define ADC_BAT_POLL_MS         1000
#endif
#ifndef ADC_BAT_AVG_N
#define ADC_BAT_AVG_N           8
#endif

#ifndef ADC_BAT_VREFINT_MV
#define ADC_BAT_VREFINT_MV      1200
#endif

/** 电量刻度：0,1,2,...,100 共 101 档 */
#ifndef ADC_BAT_PCT_MAX
#define ADC_BAT_PCT_MAX         100
#endif
#define ADC_BAT_PCT_NUM         (ADC_BAT_PCT_MAX + 1)

/**
 * 保护 / 预警（百分比档位 + 滞回）
 * 按最新 OCV 表反查：3.3V≈7%，3.5V≈12%（3.4V≈9%，3.6V≈17%）
 */
#ifndef ADC_BAT_PROTECT_ENTER_PCT
#define ADC_BAT_PROTECT_ENTER_PCT   7   /* ≈3.3V */
#endif
#ifndef ADC_BAT_PROTECT_EXIT_PCT
#define ADC_BAT_PROTECT_EXIT_PCT    9   /* ≈3.4V */
#endif
#ifndef ADC_BAT_WARN_ENTER_PCT
#define ADC_BAT_WARN_ENTER_PCT      12  /* ≈3.5V */
#endif
#ifndef ADC_BAT_WARN_EXIT_PCT
#define ADC_BAT_WARN_EXIT_PCT       17  /* ≈3.6V */
#endif

typedef enum
{
    ADC_BAT_LVL_OK = 0,
    ADC_BAT_LVL_WARN,
    ADC_BAT_LVL_PROTECT,
} adc_bat_level_t;

typedef struct
{
    uint16_t vbat_mv;
    uint16_t vdda_mv;
    uint16_t vpin_mv;
    uint16_t raw_bat;
    uint16_t raw_vref;
    uint8_t  percent;       /**< 0~100，共 101 档 */
    adc_bat_level_t level;
    uint8_t  valid;
} adc_bat_sample_t;

int adc_bat_init(void);
int adc_bat_read(adc_bat_sample_t *out);

uint16_t adc_bat_get_mv(void);
uint16_t adc_bat_get_vdda_mv(void);
uint8_t  adc_bat_get_percent(void);
adc_bat_level_t adc_bat_get_level(void);

#ifdef __cplusplus
}
#endif

#endif /* __ADC_BAT_H__ */
