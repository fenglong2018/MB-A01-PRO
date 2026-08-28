# USB CDC 流程图

板级时钟/引脚：`board/usb_hw.*`。本目录：会话与 `cdc_acm_write`。插入/拔出滤波见 [`BSP/key/flow.md`](../../BSP/key/flow.md)。

```mermaid
flowchart TD
  subgraph 下行
    PC1[主机] --> OUT[CDC OUT]
    OUT --> CB[RX 回调]
    CB --> CLI[cli 缓冲]
  end
  subgraph 上行
    W1[ulog / stream / JSON rsp] --> W[cdc_acm_write]
    W --> IN[CDC IN 64B 分包]
    IN --> PC2[主机]
  end
  DTR[打开串口 DTR=1] --> LOG[ulog 才真发 / 横幅一次]
  KIN[KEY USB_IN] --> ST[usb_cdc_start 一次]
  KOUT[KEY USB_OUT] --> SP[usb_cdc_stop]
```

插 USB：PA7 或 `usb_cdc_is_on()` → idle 不 STOP0。CHARGE 另有 MODE `pm_lock`。确认在位期间不 `stop+start`。
