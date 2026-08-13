/**
 * @file adc_bat.c
 * @brief 电池 ADC：自校准 + VREFINT 算 VDDA + 分压还原 Vbat
 *
 * 不使用 DMA：双通道低频软件触发即可。
 * 分压源阻高，采样时间取 239.5 cycles。
 */
#include <rtthread.h>
#include "config.h"
#include "adc_bat.h"
#include "adc_bat_soc.h"
#include "board_pins.h"
#include "mode.h"
#if USE_LED
#include "led.h"
#endif
#include "n32wb452.h"
#include "n32wb452_adc.h"
#include "n32wb452_gpio.h"
#include "n32wb452_rcc.h"

#if USE_ADC_BAT

#define ADC_FULL_SCALE      4095u

/* Vbat = Vpin * (R16+R17)/R17 = Vpin * 45/33 = Vpin * 15/11 */
#define BAT_DIV_NUM         15u
#define BAT_DIV_DEN         11u

static struct rt_thread s_adc_thread;
static rt_uint8_t s_adc_stack[ADC_BAT_THREAD_STACK];
static struct rt_mutex s_lock;
static adc_bat_sample_t s_last;
static adc_bat_level_t s_level = ADC_BAT_LVL_OK;
static uint8_t s_ready;

static uint16_t adc_read_raw(uint8_t ch)
{
    ADC_ConfigRegularChannel(ADC1, ch, 1, ADC_SAMP_TIME_239CYCLES5);
    ADC_ClearFlag(ADC1, ADC_FLAG_ENDC | ADC_FLAG_STR);
    ADC_EnableSoftwareStartConv(ADC1, ENABLE);
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_ENDC) == RESET)
    {
    }
    ADC_ClearFlag(ADC1, ADC_FLAG_ENDC | ADC_FLAG_STR);
    return (uint16_t)ADC_GetDat(ADC1);
}

static uint16_t adc_read_avg(uint8_t ch, uint8_t n)
{
    uint32_t sum = 0;
    uint8_t i;

    if (n == 0)
    {
        n = 1;
    }
    for (i = 0; i < n; i++)
    {
        sum += adc_read_raw(ch);
    }
    return (uint16_t)(sum / n);
}

/**
 * OCV 表：下标 = 0..100（101 档），值 = mV。
 * 找最大 pct 使 adc_bat_ocv_mv[pct] <= mv。
 */
static uint8_t percent_from_mv(uint16_t mv)
{
    int lo = 0;
    int hi = ADC_BAT_PCT_MAX;
    int mid;

    if (mv <= adc_bat_ocv_mv[0])
    {
        return 0;
    }
    if (mv >= adc_bat_ocv_mv[ADC_BAT_PCT_MAX])
    {
        return (uint8_t)ADC_BAT_PCT_MAX;
    }

    while (lo < hi)
    {
        mid = (lo + hi + 1) / 2;
        if (adc_bat_ocv_mv[mid] <= mv)
        {
            lo = mid;
        }
        else
        {
            hi = mid - 1;
        }
    }
    return (uint8_t)lo;
}

static void level_update(uint8_t pct)
{
    switch (s_level)
    {
    case ADC_BAT_LVL_PROTECT:
        if (pct >= ADC_BAT_PROTECT_EXIT_PCT)
        {
            s_level = (pct < ADC_BAT_WARN_ENTER_PCT) ? ADC_BAT_LVL_WARN
                                                     : ADC_BAT_LVL_OK;
        }
        break;
    case ADC_BAT_LVL_WARN:
        if (pct < ADC_BAT_PROTECT_ENTER_PCT)
        {
            s_level = ADC_BAT_LVL_PROTECT;
        }
        else if (pct >= ADC_BAT_WARN_EXIT_PCT)
        {
            s_level = ADC_BAT_LVL_OK;
        }
        break;
    case ADC_BAT_LVL_OK:
    default:
        if (pct < ADC_BAT_PROTECT_ENTER_PCT)
        {
            s_level = ADC_BAT_LVL_PROTECT;
        }
        else if (pct < ADC_BAT_WARN_ENTER_PCT)
        {
            s_level = ADC_BAT_LVL_WARN;
        }
        break;
    }
}

static const char *level_name(adc_bat_level_t lv)
{
    switch (lv)
    {
    case ADC_BAT_LVL_OK:      return "OK";
    case ADC_BAT_LVL_WARN:    return "WARN";
    case ADC_BAT_LVL_PROTECT: return "PROTECT";
    default:                  return "?";
    }
}

static void adc_hw_init(void)
{
    GPIO_InitType gpio;
    ADC_InitType adc;

    RCC_EnableAPB2PeriphClk(BAT_ADC_BAT_CLK | RCC_APB2_PERIPH_AFIO, ENABLE);
    RCC_EnableAHBPeriphClk(RCC_AHB_PERIPH_ADC1, ENABLE);
    ADC_ConfigClk(ADC_CTRL3_CKMOD_AHB, RCC_ADCHCLK_DIV16);
    RCC_ConfigAdc1mClk(RCC_ADC1MCLK_SRC_HSE, RCC_ADC1MCLK_DIV8);

    GPIO_InitStruct(&gpio);
    gpio.Pin        = BAT_ADC_BAT_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AIN;
    GPIO_InitPeripheral(BAT_ADC_BAT_PORT, &gpio);

    ADC_DeInit(ADC1);
    ADC_InitStruct(&adc);
    adc.WorkMode       = ADC_WORKMODE_INDEPENDENT;
    adc.MultiChEn      = DISABLE;
    adc.ContinueConvEn = DISABLE;
    adc.ExtTrigSelect  = ADC_EXT_TRIGCONV_NONE;
    adc.DatAlign       = ADC_DAT_ALIGN_R;
    adc.ChsNumber      = 1;
    ADC_Init(ADC1, &adc);

    ADC_EnableTempSensorVrefint(ENABLE);

    ADC_Enable(ADC1, ENABLE);
    while (ADC_GetFlagStatusNew(ADC1, ADC_FLAG_RDY) == RESET)
    {
    }

    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1))
    {
    }
}

static void adc_sample_once(void)
{
    uint16_t raw_vref;
    uint16_t raw_bat;
    uint32_t vdda_mv;
    uint32_t vpin_mv;
    uint32_t vbat_mv;
    adc_bat_sample_t smp;
    adc_bat_level_t prev;

    raw_vref = adc_read_avg(ADC_CH_INT_VREF, ADC_BAT_AVG_N);
    raw_bat  = adc_read_avg(BAT_ADC_BAT_ADC_CH, ADC_BAT_AVG_N);

    if ((raw_vref == 0) || (raw_vref > ADC_FULL_SCALE))
    {
        return;
    }

    /* VDDA = VREFINT * FULL / raw_vref */
    vdda_mv = ((uint32_t)ADC_BAT_VREFINT_MV * ADC_FULL_SCALE) / raw_vref;
    vpin_mv = (vdda_mv * raw_bat) / ADC_FULL_SCALE;
    vbat_mv = (vpin_mv * BAT_DIV_NUM) / BAT_DIV_DEN;

    if (vbat_mv > 6000u)
    {
        vbat_mv = 6000u; /* 异常钳位，避免乱跳 */
    }

    smp.vbat_mv  = (uint16_t)vbat_mv;
    smp.vdda_mv  = (uint16_t)vdda_mv;
    smp.vpin_mv  = (uint16_t)vpin_mv;
    smp.raw_bat  = raw_bat;
    smp.raw_vref = raw_vref;
    smp.percent  = percent_from_mv(smp.vbat_mv);
    smp.valid    = 1;

    prev = s_level;
    level_update(smp.percent);
    smp.level = s_level;

    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    s_last = smp;
    rt_mutex_release(&s_lock);

    if ((prev != s_level) || !s_ready)
    {
        rt_kprintf("[ADC] %u%% (%umV) VDDA=%umV %s\n",
                   (unsigned)smp.percent,
                   (unsigned)smp.vbat_mv,
                   (unsigned)smp.vdda_mv,
                   level_name(smp.level));
    }

    /* 进入保护：通知 MODE → FORCE_OFF（CHARGE 内 MODE 自行忽略） */
    if ((s_level == ADC_BAT_LVL_PROTECT) && (prev != ADC_BAT_LVL_PROTECT))
    {
        mode_post_event(MODE_EVT_BAT_PROTECT);
    }
    /* 进入预警：通知 MODE → LOW_BATT（仅 ON 时 MODE 受理） */
    else if ((s_level == ADC_BAT_LVL_WARN) && (prev != ADC_BAT_LVL_WARN) &&
             (prev != ADC_BAT_LVL_PROTECT))
    {
        mode_post_event(MODE_EVT_BAT_WARN);
    }

#if USE_LED
    led_post_percent(smp.percent);
#endif
}

static void adc_thread_entry(void *param)
{
    (void)param;
    while (1)
    {
        adc_sample_once();
        s_ready = 1;
        rt_thread_mdelay(ADC_BAT_POLL_MS);
    }
}

int adc_bat_init(void)
{
    rt_err_t err;

    rt_memset(&s_last, 0, sizeof(s_last));
    s_level = ADC_BAT_LVL_OK;
    s_ready = 0;

    rt_mutex_init(&s_lock, "adcbat", RT_IPC_FLAG_PRIO);
    adc_hw_init();

    /* 上电先采一次，便于 MODE 后续立刻读 */
    adc_sample_once();
    s_ready = 1;

    err = rt_thread_init(&s_adc_thread,
                         "adcbat",
                         adc_thread_entry,
                         RT_NULL,
                         s_adc_stack,
                         sizeof(s_adc_stack),
                         ADC_BAT_THREAD_PRIO,
                         20);
    if (err != RT_EOK)
    {
        rt_kprintf("[ADC] thread init failed\n");
        return -1;
    }
    rt_thread_startup(&s_adc_thread);
    rt_kprintf("[ADC] ready\n");
    return 0;
}

int adc_bat_read(adc_bat_sample_t *out)
{
    if ((out == RT_NULL) || !s_ready)
    {
        return -1;
    }
    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    *out = s_last;
    rt_mutex_release(&s_lock);
    return out->valid ? 0 : -1;
}

uint16_t adc_bat_get_mv(void)
{
    adc_bat_sample_t s;
    if (adc_bat_read(&s) != 0)
    {
        return 0;
    }
    return s.vbat_mv;
}

uint16_t adc_bat_get_vdda_mv(void)
{
    adc_bat_sample_t s;
    if (adc_bat_read(&s) != 0)
    {
        return 0;
    }
    return s.vdda_mv;
}

uint8_t adc_bat_get_percent(void)
{
    adc_bat_sample_t s;
    if (adc_bat_read(&s) != 0)
    {
        return 0;
    }
    return s.percent;
}

adc_bat_level_t adc_bat_get_level(void)
{
    adc_bat_sample_t s;
    if (adc_bat_read(&s) != 0)
    {
        return ADC_BAT_LVL_OK;
    }
    return s.level;
}

static int app_adc_bat_init(void)
{
    return adc_bat_init();
}
INIT_APP_EXPORT(app_adc_bat_init);

#endif /* USE_ADC_BAT */
