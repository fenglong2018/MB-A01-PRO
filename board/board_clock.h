/**
 * @file board_clock.h
 * @brief 低功耗醒来后恢复系统时钟（HSE+PLL / SysTick / USB）
 */
#ifndef __BOARD_CLOCK_H__
#define __BOARD_CLOCK_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** STOP0/STOP 唤醒后调用：恢复与上电等效的 SYSCLK，并重配 SysTick */
void board_clock_resume_after_stop(void);

/**
 * @brief 上电早期调用：保证 SYSCLK 挂在 PLL 上
 *
 * SystemInit 遇到 HSE 不起振只会退回裸 HSI 8M 并且不开 PLL，USB 分频器就没了
 * 输入。这里补一条 HSI/2 × 18 = 72M 的兜底，USB 仍能取到 48M。
 */
void board_clock_ensure_pll(void);

/** 当前 PLL 输出频率，PLL 未运行返回 0 */
uint32_t board_clock_pll_hz(void);

/** 系统时钟是否跑在 HSE 上（0 表示 HSE 起振失败、已退到 HSI） */
int board_clock_hse_ok(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_CLOCK_H__ */
