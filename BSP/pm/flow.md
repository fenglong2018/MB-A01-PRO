# PM / STOP 流程图

MCU 睡：本目录。射频轨：`BSP/pwr`。细则：[`docs/low_power_stop2.md`](../../docs/low_power_stop2.md)。

用法：`{"cmd":"test.pm"}`；`lock`/`unlock`/`hold_ms`。

```mermaid
flowchart TD
  I[tidle] --> H[pm_idle_hook]
  H --> L{lock 或 USB_IN?}
  L -->|是| S[跳过，PLL/USB 48M 仍在]
  L -->|否| T[STOP0]
  T --> R[醒来 board_clock_resume]
```

```mermaid
flowchart TD
  A{MODE} --> B[OFF 或 FORCE_OFF]
  B --> C[led_output_off + 关电源脚]
  C --> D[iwdg_stop2_quiet]
  D --> E[关中断后再拉灭 LED]
  E --> F[切 .rram 栈]
  F --> G[STOP2]
  G --> H[KEY/USB 醒]
  H --> I[软件复位]
```

已 lock：MODE 非 FAKE_OFF；KEY 滤波；nvflash；会话发信。USB 线：idle 读 `board_usb_inserted()`。
仅 `FAKE_OFF` 放行 STOP0（RTC 10s 闪灯）。OFF/FORCE_OFF 走 STOP2。
