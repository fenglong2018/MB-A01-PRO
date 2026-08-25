/**
 * @file iwdg.h
 * @brief 独立看门狗。不走 RT_USING_WDT（避免关 LSI、超时算错）。
 *
 * 策略：仅非 OFF/FORCE_OFF 开启；超时约 26s（>16s）。一旦 Enable 硬件通常关不掉。
 * 真关机 STOP2 前关掉 IWDG 的 STOP 唤醒/复位位，避免深睡被狗咬。
 */
#ifndef __BSP_IWDG_H__
#define __BSP_IWDG_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifndef IWDG_TIMEOUT_MS
#define IWDG_TIMEOUT_MS         26000
#endif

void iwdg_start(void);
void iwdg_feed(void);
int  iwdg_is_started(void);
/** STOP2 前调用：禁止 IWDG 在 STOP 中唤醒/复位 */
void iwdg_stop2_quiet(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_IWDG_H__ */
