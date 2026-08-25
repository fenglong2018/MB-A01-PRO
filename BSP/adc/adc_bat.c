/**
 * @file adc_bat.c
 * @brief 电池 ADC：自校准 + VREFINT 算 VDDA + 分压还原 Vbat
 *
 * 上电采 1 次；之后等 MODE/SESSION 通知。充电查表减 charge_offset_mv。
 * 分压源阻高，采样时间取 239.5 cycles。
 */
#include <rtthread.h>
#include "config.h"
#include "adc_bat.h"
#include "adc_bat_soc.h"
#include "board_pins.h"
#include "mode.h"
#include "cfg.h"
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
#define ADC_EVT_SAMPLE      (1u << 0)

static struct rt_thread s_adc_thread;
static rt_uint8_t s_adc_stack[ADC_BAT_THREAD_STACK];
static struct rt_mutex s_lock;
static struct rt_event s_kick_ev;
static struct rt_semaphore s_done;
static struct rt_timer s_charge_tmr;

static adc_bat_sample_t s_last;
static adc_bat_level_t s_level = ADC_BAT_LVL_OK;
static uint8_t s_ready;
static uint8_t s_inited;
static volatile uint8_t s_charging;
static volatile uint8_t s_skip_session_once;

static uint16_t adc_read_raw(ADC_Module *adc, uint8_t ch)
{
    ADC_ConfigRegularChannel(adc, ch, 1, ADC_SAMP_TIME_239CYCLES5);
    ADC_ClearFlag(adc, ADC_FLAG_ENDC | ADC_FLAG_STR);
    ADC_EnableSoftwareStartConv(adc, ENABLE);
    while (ADC_GetFlagStatus(adc, ADC_FLAG_ENDC) == RESET)
    {
    }
    ADC_ClearFlag(adc, ADC_FLAG_ENDC | ADC_FLAG_STR);
    return (uint16_t)ADC_GetDat(adc);
}

static uint16_t adc_read_avg(ADC_Module *adc, uint8_t ch, uint8_t n)
{
    uint32_t sum = 0;
    uint8_t i;

    if (n == 0)
    {
        n = 1;
    }
    for (i = 0; i < n; i++)
    {
        sum += adc_read_raw(adc, ch);
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

static uint16_t lookup_mv_from_vbat(uint32_t vbat_mv, uint8_t charging)
{
    uint16_t offset;

    if (!charging || (vbat_mv >= ADC_BAT_CHARGE_CV_MV))
    {
        return (uint16_t)vbat_mv;
    }
    offset = cfg_get_charge_offset_mv();
    if (offset == 0)
    {
        return (uint16_t)vbat_mv;
    }
    if (vbat_mv <= offset)
    {
        return 0;
    }
    return (uint16_t)(vbat_mv - offset);
}

static void adc_unit_init(ADC_Module *adc)
{
    ADC_InitType init;

    ADC_DeInit(adc);
    ADC_InitStruct(&init);
    init.WorkMode       = ADC_WORKMODE_INDEPENDENT;
    init.MultiChEn      = DISABLE;
    init.ContinueConvEn = DISABLE;
    init.ExtTrigSelect  = ADC_EXT_TRIGCONV_NONE;
    init.DatAlign       = ADC_DAT_ALIGN_R;
    init.ChsNumber      = 1;
    ADC_Init(adc, &init);

    ADC_Enable(adc, ENABLE);
    while (ADC_GetFlagStatusNew(adc, ADC_FLAG_RDY) == RESET)
    {
    }

    ADC_StartCalibration(adc);
    while (ADC_GetCalibrationStatus(adc))
    {
    }
}

static void adc_hw_init(void)
{
    GPIO_InitType gpio;

    RCC_EnableAPB2PeriphClk(BAT_ADC_BAT_CLK | RCC_APB2_PERIPH_AFIO, ENABLE);
    RCC_EnableAHBPeriphClk(RCC_AHB_PERIPH_ADC1 | RCC_AHB_PERIPH_ADC2, ENABLE);
    ADC_ConfigClk(ADC_CTRL3_CKMOD_AHB, RCC_ADCHCLK_DIV16);
    RCC_ConfigAdc1mClk(RCC_ADC1MCLK_SRC_HSE, RCC_ADC1MCLK_DIV8);

    GPIO_InitStruct(&gpio);
    gpio.Pin        = BAT_ADC_BAT_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AIN;
    GPIO_InitPeripheral(BAT_ADC_BAT_PORT, &gpio);

    ADC_EnableTempSensorVrefint(ENABLE);
    adc_unit_init(ADC1); /* VREFINT → VDDA */
    adc_unit_init(ADC2); /* PA4 AD_BAT */
}

static void adc_post_done(void)
{
    /* 保持二值：有人在等则唤醒，无人则保持 1 */
    (void)rt_sem_trytake(&s_done);
    rt_sem_release(&s_done);
}

static void adc_sample_once(void)
{
    uint16_t raw_vref;
    uint16_t raw_bat;
    uint32_t vdda_mv;
    uint32_t vpin_mv;
    uint32_t vbat_mv;
    uint8_t charging;
    adc_bat_sample_t smp;
    adc_bat_level_t prev;

    raw_vref = adc_read_avg(ADC1, ADC_CH_INT_VREF, ADC_BAT_AVG_N);
    raw_bat  = adc_read_avg(BAT_ADC_BAT_ADC, BAT_ADC_BAT_ADC_CH, ADC_BAT_AVG_N);

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

    charging = s_charging;
    smp.vbat_mv     = (uint16_t)vbat_mv;
    smp.v_lookup_mv = lookup_mv_from_vbat(vbat_mv, charging);
    smp.vdda_mv     = (uint16_t)vdda_mv;
    smp.vpin_mv     = (uint16_t)vpin_mv;
    smp.raw_bat     = raw_bat;
    smp.raw_vref    = raw_vref;
    smp.charging    = charging;
    smp.percent     = percent_from_mv(smp.v_lookup_mv);
    smp.valid       = 1;

    prev = s_level;
    level_update(smp.percent);
    smp.level = s_level;

    rt_mutex_take(&s_lock, RT_WAITING_FOREVER);
    s_last = smp;
    rt_mutex_release(&s_lock);

    if ((prev != s_level) || !s_ready)
    {
        rt_kprintf("[ADC] %u%% meas=%umV lookup=%umV chg=%u %s\n",
                   (unsigned)smp.percent,
                   (unsigned)smp.vbat_mv,
                   (unsigned)smp.v_lookup_mv,
                   (unsigned)smp.charging,
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
    else if ((s_level == ADC_BAT_LVL_OK) && (prev != ADC_BAT_LVL_OK) && !charging)
    {
        /* 只在空载回到 OK 时清 LOW_BATT 已发闩；充电查表虚高不当恢复 */
        mode_post_event(MODE_EVT_BAT_OK);
    }

#if USE_LED
    if (s_ready)
    {
        led_post_percent(smp.percent);
    }
#endif
}

static void adc_kick(void)
{
    if (!s_inited)
    {
        return;
    }
    (void)rt_event_send(&s_kick_ev, ADC_EVT_SAMPLE);
}

static void charge_tmr_cb(void *param)
{
    (void)param;
    adc_kick();
}

static void adc_thread_entry(void *param)
{
    rt_uint32_t set;

    (void)param;
    while (1)
    {
        set = 0;
        if (rt_event_recv(&s_kick_ev, ADC_EVT_SAMPLE,
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          RT_WAITING_FOREVER, &set) != RT_EOK)
        {
            continue;
        }
        adc_sample_once();
        s_ready = 1;
        adc_post_done();
    }
}

void adc_bat_request(void)
{
    s_skip_session_once = 0;
    adc_kick();
}

int adc_bat_sample_wait(rt_int32_t timeout_ms)
{
    rt_int32_t ticks;

    if (!s_inited)
    {
        return -1;
    }
    if (s_skip_session_once)
    {
        s_skip_session_once = 0;
        return s_ready ? 0 : -1;
    }

    ticks = (timeout_ms < 0) ? RT_WAITING_FOREVER
                             : (rt_int32_t)rt_tick_from_millisecond((rt_uint32_t)timeout_ms);
    (void)rt_sem_trytake(&s_done);
    adc_kick();
    if (rt_sem_take(&s_done, ticks) != RT_EOK)
    {
        return -1;
    }
    return s_ready ? 0 : -1;
}

void adc_bat_set_charging(uint8_t on)
{
    if (!s_inited)
    {
        return;
    }
    if (on)
    {
        s_charging = 1;
        s_skip_session_once = 0;
        rt_timer_start(&s_charge_tmr);
        adc_kick();
    }
    else
    {
        s_charging = 0;
        s_skip_session_once = 1;
        rt_timer_stop(&s_charge_tmr);
    }
}

void adc_bat_pause(void)
{
    if (!s_inited)
    {
        return;
    }
    s_charging = 0;
    rt_timer_stop(&s_charge_tmr);
}

int adc_bat_init(void)
{
    rt_err_t err;
    rt_tick_t period;

    rt_memset(&s_last, 0, sizeof(s_last));
    s_level = ADC_BAT_LVL_OK;
    s_ready = 0;
    s_inited = 0;
    s_charging = 0;
    s_skip_session_once = 0;

    rt_mutex_init(&s_lock, "adcbat", RT_IPC_FLAG_PRIO);
    rt_event_init(&s_kick_ev, "adckick", RT_IPC_FLAG_FIFO);
    rt_sem_init(&s_done, "adcdone", 0, RT_IPC_FLAG_PRIO);

    period = rt_tick_from_millisecond(ADC_BAT_CHARGE_PERIOD_MS);
    rt_timer_init(&s_charge_tmr, "adcchg", charge_tmr_cb, RT_NULL, period,
                  RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_SOFT_TIMER);

    adc_hw_init();

    /* 上电先采一次（空载公式），便于 MODE 立刻读保护 */
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
    s_inited = 1;
    rt_kprintf("[ADC] ready (notify)\n");
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
INIT_ENV_EXPORT(app_adc_bat_init);

#endif /* USE_ADC_BAT */
