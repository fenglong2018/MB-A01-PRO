/**
 * @file cdc_acm.c
 * @brief Nations USB CDC 应用层，对齐旧 wsl-ubuntu-gcc-passthrough 的 usb_cdc_pt.c
 *
 * 官方 usb_endp 通过 USART_Rx_Buffer / USB_To_USART_Send_Data 收发；
 * 这里把 USART 换成环形缓冲 + CLI/ulog 回调。
 */
#include "cdc_acm.h"
#include "usb_lib.h"
#include "usb_desc.h"
#include "usb_pwr.h"
#include "hw_config.h"
#include "product_config.h"
#include <rthw.h>
#include <rtthread.h>

uint8_t USART_Rx_Buffer[USART_RX_DATA_SIZE];
uint32_t USART_Rx_ptr_in  = 0;
uint32_t USART_Rx_ptr_out = 0;
uint32_t USART_Rx_length  = 0;
uint8_t  USB_Tx_State     = 0;

volatile uint8_t dtr_enable = 0;

static void (*s_rx_cb)(const uint8_t *data, uint32_t len);

void cdc_acm_set_rx_callback(void (*cb)(const uint8_t *data, uint32_t len))
{
    s_rx_cb = cb;
}

uint8_t cdc_acm_is_dtr_enable(void)
{
    return dtr_enable;
}

void USB_To_USART_Send_Data(uint8_t *data_buffer, uint8_t Nb_bytes)
{
    if ((s_rx_cb != RT_NULL) && (data_buffer != RT_NULL) && (Nb_bytes > 0))
    {
        s_rx_cb(data_buffer, (uint32_t)Nb_bytes);
    }
}

void Handle_USBAsynchXfer(void)
{
    uint16_t USB_Tx_ptr;
    uint16_t USB_Tx_length;

    if (USB_Tx_State != 0)
    {
        return;
    }

    if (USART_Rx_ptr_out == USART_RX_DATA_SIZE)
    {
        USART_Rx_ptr_out = 0;
    }

    if (USART_Rx_ptr_out == USART_Rx_ptr_in)
    {
        return;
    }

    if (USART_Rx_ptr_out > USART_Rx_ptr_in)
    {
        USART_Rx_length = USART_RX_DATA_SIZE - USART_Rx_ptr_out;
    }
    else
    {
        USART_Rx_length = USART_Rx_ptr_in - USART_Rx_ptr_out;
    }

    if (USART_Rx_length > VIRTUAL_COM_PORT_DATA_SIZE)
    {
        USB_Tx_ptr        = (uint16_t)USART_Rx_ptr_out;
        USB_Tx_length     = VIRTUAL_COM_PORT_DATA_SIZE;
        USART_Rx_ptr_out += VIRTUAL_COM_PORT_DATA_SIZE;
        USART_Rx_length  -= VIRTUAL_COM_PORT_DATA_SIZE;
        USB_Tx_State      = 1;
    }
    else
    {
        USB_Tx_ptr        = (uint16_t)USART_Rx_ptr_out;
        USB_Tx_length     = (uint16_t)USART_Rx_length;
        USART_Rx_ptr_out += USART_Rx_length;
        USART_Rx_length   = 0;
        USB_Tx_State      = (USB_Tx_length == VIRTUAL_COM_PORT_DATA_SIZE) ? 2 : 1;
    }

    USB_CopyUserToPMABuf(&USART_Rx_Buffer[USB_Tx_ptr], ENDP1_TXADDR, USB_Tx_length);
    USB_SetEpTxCnt(ENDP1, USB_Tx_length);
    USB_SetEpTxValid(ENDP1);
}

uint32_t cdc_acm_write(const uint8_t *data, uint32_t len)
{
    uint32_t i;
    rt_base_t level;

    if ((data == RT_NULL) || (len == 0))
    {
        return 0;
    }

    for (i = 0; i < len; i++)
    {
        uint32_t next;

        level = rt_hw_interrupt_disable();
        next  = USART_Rx_ptr_in + 1U;
        if (next == USART_RX_DATA_SIZE)
        {
            next = 0;
        }
        if (next == USART_Rx_ptr_out)
        {
            rt_hw_interrupt_enable(level);
            break;
        }
        USART_Rx_Buffer[USART_Rx_ptr_in] = data[i];
        USART_Rx_ptr_in = next;
        rt_hw_interrupt_enable(level);
    }

    return i;
}

void cdc_acm_data_send_with_dtr_test(void)
{
    static const uint8_t data_buffer[10] = {
        0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A
    };

    if (dtr_enable)
    {
        (void)cdc_acm_write(data_buffer, 10);
    }
}

static uint8_t s_cdc_on;

void cdc_acm_init(void)
{
    Set_USBClock();
    USB_Init();
}

void usb_cdc_start(void)
{
    if (s_cdc_on)
    {
        return;
    }
    cdc_acm_init();
    s_cdc_on = 1;
}

void usb_cdc_stop(void)
{
    usb_hw_deinit();
    s_cdc_on = 0;
    dtr_enable = 0;
}
