# BSP/pm — MCU 浅睡（STOP0）

> 与板级射频电源 `BSP/pwr` 分工：本目录只管 **MCU STOP0**；PLNA 等仍用 `pwr_plna_*`。

## 开关

`product_config.h` → `USE_PM=1`

## 行为

```text
tidle（空闲线程）
  → pm_idle_hook（rt_thread_idle_sethook）
  → lock==0 ? PWR STOP0 : 跳过
  → 醒来 board_clock_resume_after_stop()（HSE+PLL+SysTick+USB clk）
```

- **不解耦业务**：不 `#include mode/gnss`；忙时由调用方 `pm_lock()` / `pm_unlock()`。
- **假关机 / STOP2**：不在本模块；见 `docs/low_power_stop2.md`。

## API

| API | 说明 |
|-----|------|
| `pm_lock` / `pm_unlock` | 嵌套禁止入睡 |
| `pm_lock_count` | 当前禁止计数 |
| `pm_idle_hook_install` | 注册 hook（`INIT_APP_EXPORT` 已自动调） |

## 文件

| 文件 | 说明 |
|------|------|
| `pm.h` | 对外 API |
| `pm_stop0.c` | lock + idle hook + STOP0 |
| `board/board_clock.*` | 醒后时钟恢复（与 pm 分离） |

## 后续可接 lock 的点（未强制）

- GNSS/RDSS 会话开始/结束
- PASSTHRU 进入/退出
- USB/CLI 调试期（可选）

当前未自动 lock：SysTick 仍约 10ms 唤醒一次，属预期浅睡。
