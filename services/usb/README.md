# services/usb

USB CDC ACM（Nations usbfs 栈）。插 USB 出 COM，上位机拉 DTR 后 `ping`。

流程图：[`flow.md`](flow.md)。

> **变更记录（2026-08-26）**  
> KEY 一检测到插入即 `usb_cdc_start()`。PA11/PA12 完全不 `GPIO_Init`（对齐 DEMO）；USBCLK=PLL/3。确认拔出才 `usb_cdc_stop()`。  
> 未插不 `USB_Init`。idle 禁 STOP0 看 PA7 **或** `usb_cdc_is_on()`。

| 文件 | 说明 |
|------|------|
| `cdc_acm.c` | CDC 收发；**仅插入确认后** `usb_cdc_start()` |
| `nations/` | 官方 Virtual_COM_Port 协议栈 |
| `cdc_io.h` | 薄 IO 接口（避免拉入 `usb_cdc.h`） |
| `usb_app.*` | 可选封装入口 |

板级 USB 时钟/引脚在 `board/usb_hw.*`。宏见 `config/cdc_acm.h`。
