/**
 * @file board_gpio.h
 * @brief 上电最早 GPIO：电源使能脚关、LED 灭、已知输入/ADC
 */
#ifndef __BOARD_GPIO_H__
#define __BOARD_GPIO_H__

#ifdef __cplusplus
extern "C" {
#endif

/** 须在 GNSS/RDSS/LED 模块 init 之前调用（rt_hw_init 内） */
void board_gpio_early_init(void);

/** 已配成输出后：全部电源使能 + LED 拉到关电平（关机 / FORCE_OFF） */
void board_gpio_outputs_off(void);

/** MCU_I_USB_IN：读到 USB_IN_INSERTED_LEVEL 为插入。idle/MODE 共用。 */
int board_usb_inserted(void);

/**
 * 复位最早期采到的按键电平（`board_gpio_early_init` 里锁存）。
 * 只反映复位瞬间的脚，不把 STOP2 短按脉冲伪装成「仍按着」。
 * KEY 用这些电平 + `board_boot_from_stop2()` 接回轮询。
 */
int board_boot_sos_down(void);
int board_boot_fall_active(void);
/** 1=这次复位来自 STOP2 唤醒（BKP 戳，early_init 已清） */
int board_boot_from_stop2(void);

/** PM 写入、early_init 读出的 STOP2 唤醒戳（BKP_DAT42） */
#define BOARD_STOP2_WAKE_MAGIC  ((uint16_t)0xA50Bu)

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_GPIO_H__ */
