/**
 * @file board_clock.h
 * @brief 低功耗醒来后恢复系统时钟（HSE+PLL / SysTick / USB）
 */
#ifndef __BOARD_CLOCK_H__
#define __BOARD_CLOCK_H__

#ifdef __cplusplus
extern "C" {
#endif

/** STOP0/STOP 唤醒后调用：恢复与上电等效的 SYSCLK，并重配 SysTick */
void board_clock_resume_after_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_CLOCK_H__ */
