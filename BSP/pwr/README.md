# BSP/pwr — 共享电源轨

| API | 脚 | 说明 |
|-----|-----|------|
| `pwr_plna_acquire/release` | `MCU_EN_PLNA` | GNSS / RDSS 共享，引用计数到 0 才关 |

GNSS 专用脚（`EN_PGNSS` / `EN_LNA_GNSS`）由 `BSP/gnss` 自管。

## 与 MCU 低功耗的分工

| 层 | 职责 |
|----|------|
| 本目录 | **板级射频电源轨**（PLNA 等），会话结束应 release 到 0 |
| MCU **STOP0** 浅睡 | [`BSP/pm`](../pm/README.md)：idle hook（已实施） |
| MCU **STOP2** / 假关机 | [`docs/low_power_stop2.md`](../../docs/low_power_stop2.md)（方案，未实施） |

进长睡 `SLEEP` 前：本模块引用计数须为 0（射频已关）。
