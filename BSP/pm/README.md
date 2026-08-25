# BSP/pm — MCU 浅睡（STOP0）与 STOP2

> 与板级射频电源 `BSP/pwr` 分工：本目录管 **MCU STOP0 / STOP2**；PLNA 等仍用 `pwr_plna_*`。

> 手册 MCU 典型电流（后续优化对照）：STOP0 **90 µA**（全 SRAM）；STOP2 **6 µA**（16KB R-SRAM + CPU 寄存器）；STANDBY **2.5 µA**（备份域 + 16KB R-SRAM，可选 RTC）。细则 `docs/low_power_stop2.md` §2.0。
>
>
> **变更记录（2026-08-18）**  
> `pm_stop2_enter()` 已接：切 `.rram` 栈 → STOP2 → 醒后软件复位。  
> **解耦：** idle 读 `board_usb_inserted()`（USB_IN 脚），有线则跳过 STOP0（PLL/USB 48M 在 STOP0 里保不住）。不经过 MODE，也不再 `pm_boot_hold`。透传仍 `pm_lock`。

## 开关

`product_config.h` → `USE_PM=1`

流程图：[`flow.md`](flow.md)。CLI：`{"cmd":"test.pm"}`。

## 行为

```text
tidle（空闲线程）
  → pm_idle_hook
  → lock≠0 或 USB_IN 插入？跳过（保住 PLL / USB 48M）
  → 否则 STOP0
  → 醒来 board_clock_resume_after_stop()（HSE+PLL+SysTick+USB clk）
```

- **不解耦业务**：不 `#include mode/gnss`。USB 线只问板级 `board_usb_inserted()`。MODE 在非 `FAKE_OFF` 时 `pm_lock`；KEY 滤波、nvflash、会话发信再叠一层。
- **假关机**：逻辑态 `FAKE_OFF` + RTC 10s 心跳；idle 仍可 STOP0。
- **真关机 STOP2**：`pm_stop2_enter()`（R-SRAM 栈 + 醒后复位）。见 `docs/low_power_stop2.md`。

## API

| API | 说明 |
|-----|------|
| `pm_lock` / `pm_unlock` | 嵌套禁止入睡 |
| `pm_lock_count` | 当前禁止计数 |
| `pm_idle_hook_install` | 注册 hook（`INIT_APP_EXPORT` 已自动调） |
| `pm_stop2_enter` | 真关机：R-SRAM 栈 + STOP2，不返回 |

## 文件

| 文件 | 说明 |
|------|------|
| `pm.h` | 对外 API（含 `pm_stop2_enter`） |
| `pm_stop0.c` | lock + idle STOP0 + STOP2 入口 |
| `board/board_clock.*` | 醒后时钟恢复（与 pm 分离） |

## 后续可接 lock 的点（未强制）

- GNSS/RDSS 会话开始/结束（发信路径已喂狗；是否 lock 可再定）

已接 lock：`nvflash` 擦写；MODE 仅透传。USB 线：idle → `board_usb_inserted()`。
