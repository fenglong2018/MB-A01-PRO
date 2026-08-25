# app/mode

工作模式机（关机 / 看电 / 开机 / 告警 / 假关机 / 充电 / 低电 / 透传）。

> **变更记录（2026-08-25）**  
> 看电 5s 整段不浅睡、灯保持；窗口内不进 LOW_BATT。有卡 ON 假关机仍 10s 闪；无卡先 5s 每秒闪再灭灯。无卡满 60min 真关机。  
>
> **变更记录（2026-08-24）**  
> STOP0 只在 `FAKE_OFF` 放行（逻辑 ON/ALARM 空闲）。关机看电、物理 ON/ALARM 发信默认不浅睡。STOP2 醒后立刻进 BATT。
>
> **变更记录（2026-08-19）**  
> 进入 `PASSTHRU` 时静音 CDC ulog，退出恢复。细则见 [fsm.md](fsm.md) 第 7 节、[`services/log/README.md`](../../services/log/README.md)。

| 文件 | 说明 |
|------|------|
| [fsm.md](fsm.md) | 按单个状态分块的流程图 |
| `mode.h` / `mode.c` | MODE 任务 + 转移 + `mode_post_event` |
| [低功耗 STOP2](../../docs/low_power_stop2.md) | FAKE_OFF=STOP0+RTC 10s；OFF/FORCE_OFF=STOP2 |
| [模块总表](../../docs/module_status.md) | 各模块完成度 |

职责：

- 接收 KEY / ADC / session 的 `MODE_EVT_*`（`rt_event`）
- 按 fsm.md 切状态；告警进出调用 `session_alarm_start/stop` / `session_alarm_resume`
- 粘性 MODE/告警锚点/`lb_sent` 只写 `bkp_user`（不写 Flash）
- 第一次 GNSS 校时锁告警节奏锚点：`mode_alarm_anchor_latch`
- 一拍 `SHOT_BUSY` / `SHOT_IDLE` → 进出 `FAKE_OFF`
- KEY 不直接改模式

逻辑告警 = 物理 `ALARM` **或** `FAKE_OFF←ALARM` **或** 告警短按叠上的 `BATT`。这三态按键同一套（短按只看电、长按退出、FALL 保持）。

VS Code / Cursor 预览 Mermaid：扩展 **Markdown Preview Mermaid Support**（Matt Bierner，`bierner.markdown-mermaid`）。打开 `fsm.md` → `Ctrl+Shift+V`。
