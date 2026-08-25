# BSP/pwr — 共享电源轨

> **变更记录（2026-08-18）**  
> 进 STOP2 前射频引用计数须为 0。MCU STOP2 已接，本目录 API 未改。

| API | 脚 | 说明 |
|-----|-----|------|
| `pwr_plna_acquire/release` | `MCU_EN_PLNA` | GNSS / RDSS 共享，引用计数到 0 才关 |

流程图：[`flow.md`](flow.md)。无 CLI 强拨（`io.*` 未接）；会话时量脚。

GNSS 专用脚（`EN_PGNSS` / `EN_LNA_GNSS`）由 `BSP/gnss` 自管。

## 与 MCU 低功耗的分工

| 层 | 职责 |
|----|------|
| 本目录 | **板级射频电源轨**（PLNA 等），会话结束应 release 到 0 |
| MCU **STOP0** 浅睡 | [`BSP/pm`](../pm/README.md)：idle hook（已实施） |
| MCU **STOP2** | [`docs/low_power_stop2.md`](../../docs/low_power_stop2.md)（2026-08-18：OFF/FORCE_OFF 已接 `pm_stop2_enter`） |

进深睡前：本模块引用计数须为 0（射频已关）。
