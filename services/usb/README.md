# services/usb

USB CDC ACM（Nations usbfs 栈）。插 USB 出 COM，上位机拉 DTR 后 `ping`。

流程图：[`flow.md`](flow.md)。

> **变更记录（2026-08-24）**  
> 枚举前关闭 SUSPM/ESOFM（对齐同芯片 CherryUSB：否则主机一直复位、不出 COM）。  
> **未插 USB 不 `USB_Init`**。插线才 `usb_cdc_start()`；拔线 / STOP2 前 `usb_cdc_stop()`（关 DP 上拉、PHY、脚改模拟），避免关机约 1.6mA。

| 文件 | 说明 |
|------|------|
| `cdc_acm.c` | CDC 收发；**仅插入 USB 时** `usb_cdc_start()` |
| `nations/` | 官方 Virtual_COM_Port 协议栈 |
| `cdc_io.h` | 薄 IO 接口（避免拉入 `usb_cdc.h`） |
| `usb_app.*` | 可选封装入口 |

板级 USB 时钟/引脚在 `board/usb_hw.*`。宏见 `config/cdc_acm.h`。
