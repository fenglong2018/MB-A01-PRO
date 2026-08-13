# app/mode

工作模式机（关机 / 电量 / 开机 / 告警 / 充电 / 低电 / 透传）。

| 文件 | 说明 |
|------|------|
| [fsm.md](fsm.md) | 状态机定稿（含 SLEEP 规划节） |
| `mode.h` / `mode.c` | MODE 任务 + 转移 + `mode_post_event` |
| [低功耗 STOP2 方案](../../docs/low_power_stop2.md) | 假关机 / 保留上下文（**未实施**） |

职责：

- 接收 KEY 上报的 `MODE_EVT_*`（`rt_event`）
- 按 fsm.md 切状态；告警进出调用 `session_alarm_start/stop`
- KEY 不直接改模式

VS Code / Cursor 预览 Mermaid 状态图：扩展市场安装 **Markdown Preview Mermaid Support**（发布者 Matt Bierner，ID：`bierner.markdown-mermaid`）。打开 `fsm.md` → 预览 Markdown（`Ctrl+Shift+V`）。
