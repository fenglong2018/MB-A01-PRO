# session 流程图

> 实现：`session_alarm.c` / `msg_pack.c`。规则与用法：[`README.md`](README.md)。

## 1. 一拍 `run_one_cycle`

LOW_BATT / `test.session.once` **不发** SHOT。ALARM / ON 在拍前后发 `SHOT_BUSY` / `SHOT_IDLE`。

```mermaid
flowchart TD
  A[cycle_and_idle] --> B[SHOT_BUSY]
  B --> C{sim_present?}
  C -->|否| Z[本拍空过]
  C -->|是| D[adc_bat_sample_wait<br/>刚离 CHARGE 第一拍可跳过]
  D --> E{level=PROTECT?}
  E -->|是| Z
  E -->|否| F[gnss_start_fix]
  F --> G{mq 定位成功?}
  G -->|否| Z
  G -->|是| H[首次 unix 可写 cfg.first_fix]
  H --> I[maybe_rtc_from_gnss]
  I --> J[ensure_card 如本机卡未知]
  J --> K[msg_pack_loca_up A或N]
  K --> L[rdss_start_send]
  L --> M[等 FKI mq]
  M --> Z
  Z --> N{ALARM或ON?}
  N -->|是| O[SHOT_IDLE]
  N -->|否| P[结束]
  O --> P
```

## 2. 校时（仅产品拍）

```mermaid
flowchart TD
  U[unix_sec 来自 GNSS RMC] --> A{unix=0?}
  A -->|是| X[不写 RTC]
  A -->|否| B{当前 ON 且本次未校过?}
  B -->|是| C[rtc_post_unix GNSS]
  B -->|否| D{ALARM 且尚未 synced?}
  D -->|是| E[post + mode_alarm_anchor_latch]
  D -->|否| X
```

RDSS / 透传 / `test.gnss.fix` / `test.session.once`：**不写 RTC**。

## 3. 节奏

见 README 状态图：ALARM 立刻 A → 2/5/10min（满 72h 仍 A）；ON 立刻 N → 10min（独立通道）；LOW_BATT 只 1 条 N。

## 使用

| 场景 | 怎么触发 |
|------|----------|
| 产品 ON/ALARM/LOW_BATT | MODE 调 `session_*`，不要 CLI 强切 MODE |
| 强制一拍 | `{"cmd":"test.session.once"}`（不改 MODE、不校时、不发 SHOT） |
