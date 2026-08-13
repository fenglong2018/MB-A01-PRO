# services/usb

USB CDC ACM 会话与应用封装（非板级 init）。

| 文件 | 说明 |
|------|------|
| `cdc_acm.c` | CherryUSB CDC 设备；`INIT_DEVICE_EXPORT` |
| `cdc_io.h` | 薄 IO 接口（避免拉入 `usb_cdc.h`） |
| `usb_app.*` | 可选封装入口 |

板级 USB 时钟/引脚在 `board/usb_hw.*`。宏见 `config/cdc_acm.h`。
