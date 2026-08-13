/**
 * @file usb_hw.h
 * @brief N32WB452 USB 底层硬件宏（DP 上拉等）
 */
#ifndef __USB_HW_H__
#define __USB_HW_H__

#include "n32wb452.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pull up controller register */
#ifndef DP_CTRL
#define DP_CTRL ((__IO unsigned *)(0x40001820))
#endif

#ifndef _EnPortPullup
#define _EnPortPullup()   (*DP_CTRL = (*DP_CTRL) | 0x10000000u)
#endif
#ifndef _DisPortPullup
#define _DisPortPullup()  (*DP_CTRL = (*DP_CTRL) & 0xEFFFFFFFu)
#endif

void USB_Interrupts_Config(void);
void Set_USBClock(void);
void usb_dc_low_level_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __USB_HW_H__ */
