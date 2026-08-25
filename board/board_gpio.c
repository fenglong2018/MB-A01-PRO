/**
 * @file board_gpio.c
 * @brief 复位后 GPIO 浮空，先把电源使能脚推挽到关电平，避免模块误上电
 */
#include "board_gpio.h"
#include "board_pins.h"
#include "n32wb452_gpio.h"
#include "n32wb452_rcc.h"
#include "n32wb452_bkp.h"
#include "n32wb452_pwr.h"

static void gpio_out_pp_level(GPIO_Module *port, uint16_t pin, int high)
{
    GPIO_InitType gpio;

    /* 先写 ODR，再切输出，避免低有效脚在 ODR=0 时瞬间打开 */
    PIN_WRITE(port, pin, high);

    GPIO_InitStruct(&gpio);
    gpio.Pin        = pin;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(port, &gpio);
    PIN_WRITE(port, pin, high);
}

static void gpio_in_floating(GPIO_Module *port, uint16_t pin)
{
    GPIO_InitType gpio;

    GPIO_InitStruct(&gpio);
    gpio.Pin       = pin;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_InitPeripheral(port, &gpio);
}

static uint8_t s_boot_sos_down;
static uint8_t s_boot_fall_active;
static uint8_t s_boot_from_stop2;

void board_gpio_outputs_off(void)
{
    PIN_WRITE(EN_5V_PA_POW_PORT, EN_5V_PA_POW_PIN, EN_5V_PA_POW_OFF_LEVEL);
    PIN_WRITE(EN_PLNA_POW_PORT, EN_PLNA_POW_PIN, EN_PLNA_POW_OFF_LEVEL);
    PIN_WRITE(EN_LNA_POW_GNSS_PORT, EN_LNA_POW_GNSS_PIN, EN_LNA_POW_GNSS_OFF_LEVEL);
    PIN_WRITE(EN_BLE_POW_PORT, EN_BLE_POW_PIN, EN_BLE_POW_OFF_LEVEL);
    PIN_WRITE(EN_PGNSS_POW_PORT, EN_PGNSS_POW_PIN, EN_PGNSS_POW_OFF_LEVEL);
    PIN_WRITE(EN_LNA_RDSS_POW_PORT, EN_LNA_RDSS_POW_PIN, EN_LNA_RDSS_POW_OFF_LEVEL);
    PIN_WRITE(EN_PRDSS_POW_PORT, EN_PRDSS_POW_PIN, EN_PRDSS_POW_OFF_LEVEL);
    PIN_WRITE(LED1_PORT, LED1_PIN, LED_OFF_LEVEL);
    PIN_WRITE(LED2_PORT, LED2_PIN, LED_OFF_LEVEL);
    PIN_WRITE(LED3_PORT, LED3_PIN, LED_OFF_LEVEL);
}

void board_gpio_early_init(void)
{
    GPIO_InitType gpio;

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA | RCC_APB2_PERIPH_GPIOB |
                            RCC_APB2_PERIPH_AFIO, ENABLE);

    /* PA15/PB3/PB4 默认 JTAG：只留 SWD，才能当电源/USART2 */
    GPIO_ConfigPinRemap(GPIO_RMP_SW_JTAG_SW_ENABLE, ENABLE);

    gpio_out_pp_level(EN_5V_PA_POW_PORT, EN_5V_PA_POW_PIN, EN_5V_PA_POW_OFF_LEVEL);
    gpio_out_pp_level(EN_PLNA_POW_PORT, EN_PLNA_POW_PIN, EN_PLNA_POW_OFF_LEVEL);
    gpio_out_pp_level(EN_LNA_POW_GNSS_PORT, EN_LNA_POW_GNSS_PIN, EN_LNA_POW_GNSS_OFF_LEVEL);
    gpio_out_pp_level(EN_BLE_POW_PORT, EN_BLE_POW_PIN, EN_BLE_POW_OFF_LEVEL);
    gpio_out_pp_level(EN_PGNSS_POW_PORT, EN_PGNSS_POW_PIN, EN_PGNSS_POW_OFF_LEVEL);
    gpio_out_pp_level(EN_LNA_RDSS_POW_PORT, EN_LNA_RDSS_POW_PIN, EN_LNA_RDSS_POW_OFF_LEVEL);
    gpio_out_pp_level(EN_PRDSS_POW_PORT, EN_PRDSS_POW_PIN, EN_PRDSS_POW_OFF_LEVEL);

    /* LED 低有效，上电灭 */
    gpio_out_pp_level(LED1_PORT, LED1_PIN, LED_OFF_LEVEL);
    gpio_out_pp_level(LED2_PORT, LED2_PIN, LED_OFF_LEVEL);
    gpio_out_pp_level(LED3_PORT, LED3_PIN, LED_OFF_LEVEL);

    GPIO_InitStruct(&gpio);
    gpio.Pin       = BAT_ADC_BAT_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_InitPeripheral(BAT_ADC_BAT_PORT, &gpio);

    gpio_in_floating(SOS_KEY_PORT, SOS_KEY_PIN);
    gpio_in_floating(USB_IN_PORT, USB_IN_PIN);
    gpio_in_floating(FALL_KEY_PORT, FALL_KEY_PIN);
    gpio_in_floating(RD_BD_SIMCARD_PORT, RD_BD_SIMCARD_PIN);

    /* USB 未枚举时 DP/DM 模拟，避免数字缓冲漏电 */
    {
        GPIO_InitType usb;

        GPIO_InitStruct(&usb);
        usb.Pin       = USB_DM_PIN | USB_DP_PIN;
        usb.GPIO_Mode = GPIO_Mode_AIN;
        GPIO_InitPeripheral(USB_DM_PORT, &usb);
    }

    /*
     * STOP2 唤醒后走 NVIC_SystemReset，EXTI 那个沿在复位里没了；
     * 这里趁最早期留一份电平，KEY 起来后据此把滤波接上。
     */
    s_boot_sos_down    = (PIN_READ(SOS_KEY_PORT, SOS_KEY_PIN) == 0) ? 1u : 0u;
    s_boot_fall_active = (PIN_READ(FALL_KEY_PORT, FALL_KEY_PIN) != 0) ? 1u : 0u;

    /* 必须在 RTC BackupReset 之前读走 */
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_PWR | RCC_APB1_PERIPH_BKP, ENABLE);
    PWR_BackupAccessEnable(ENABLE);
    s_boot_from_stop2 = (BKP_ReadBkpData(BKP_DAT42) == BOARD_STOP2_WAKE_MAGIC) ? 1u : 0u;
    if (s_boot_from_stop2)
    {
        BKP_WriteBkpData(BKP_DAT42, 0);
    }
}

int board_boot_sos_down(void)
{
    return s_boot_sos_down ? 1 : 0;
}

int board_boot_fall_active(void)
{
    return s_boot_fall_active ? 1 : 0;
}

int board_boot_from_stop2(void)
{
    return s_boot_from_stop2 ? 1 : 0;
}

int board_usb_inserted(void)
{
    return (PIN_READ(USB_IN_PORT, USB_IN_PIN) == USB_IN_INSERTED_LEVEL);
}
