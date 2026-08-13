# app/session

告警 / 开机 / 低电 调度（GNSS / RDSS）

| 阶段 | 周期 | 报文 |
|------|------|------|
| 进 ALARM | 立刻 1 次 | alarm=`A` |
| ALARM 0～24h | 每 2 分钟 | `A` |
| ALARM 24～48h | 每 5 分钟 | `A` |
| 满 48h | `MODE_EVT_ALARM_EXPIRE` → **ON** | — |
| 进 ON / 回 ON | 立刻 1 次 | 常态=`N` |
| ON | 每 **10 分钟** | `N` |
| LOW_BATT | **只发 1 条** → `MODE_EVT_LOW_BATT_DONE` | `N` |

| API | 说明 |
|-----|------|
| `session_alarm_start/stop` | ALARM 节奏 |
| `session_on_start/stop` | ON 10min |
| `session_lowbatt_once` | 低电单次 |
| `session_stop_all` | 停当前任意会话 |

| 文件 | 说明 |
|------|------|
| `session_alarm.*` | 启停 + 定时器调度（当前 `rt_timer` / tick） |
| `msg_pack.*` | MBA01 风格组包；本机卡号来自 RDSS `$BDICP` → `cfg.device_id` |

规则：`BSP/gnss/README.md`、`BSP/rdss/README.md`、`app/mode/fsm.md`

假关机 / STOP2：**不做**（见 `docs/low_power_stop2.md` 预留）。
