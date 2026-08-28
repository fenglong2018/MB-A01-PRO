# GNSS 流程图

规则与引脚：[`README.md`](README.md)。用法：CLI `test.gnss.fix` / `stream.set` `name:gnss`。串口助手逐步操作：[`docs/board_bringup.md`](../../docs/board_bringup.md) 第 0.3、7 节。

## 驱动状态

```mermaid
stateDiagram-v2
    [*] --> Off
    Off --> FixWait: gnss_start_fix
    FixWait --> Off: GGA q大于等于1 或超时\n关电 + mq
    Off --> PassThru: passthru_enter
    PassThru --> Off: passthru_exit / 保护
    FixWait --> PassThru: passthru_enter 打断定位
```

## 一拍定位

```mermaid
flowchart TD
  A[start_fix] --> B[plna_acquire + LNA + PGNSS]
  B --> C[开 USART3]
  C --> D[收 NMEA]
  D --> E{GGA quality 大于等于 1?}
  E -->|否且未超时| D
  E -->|是| F[再等同拍 RMC]
  F --> G{RMC A + ddmmyy?}
  G -->|是| H[unix_sec=UTC]
  G -->|否等到超时| I[unix_sec=0 仍算定位成功]
  E -->|超时无 GGA| J[失败 mq]
  H --> K[关电 + 成功 mq]
  I --> K
```

透传：RX → `stream_write("gnss")` → `cdc_acm_write`；PC 非 `{` 行 → `gnss_passthru_write`。不解析、不校时。

## 使用

```json
{"id":17,"cmd":"test.gnss.fix"}
{"id":3,"cmd":"stream.set","name":"gnss","enable":1}
{"id":6,"cmd":"stream.set","name":"gnss","enable":0}
```

`test.gnss.fix` **不写 RTC**。产品校时只在 session 的 ON/未校时 ALARM。关透传必须 `{` 开头。
