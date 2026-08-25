# RDSS 流程图

规则：[`README.md`](README.md)。用法：`test.rdss.card` / `test.rdss.send` / `stream.set` `rdss`。

## 驱动状态

```mermaid
stateDiagram-v2
    [*] --> Off
    Off --> CardWait: ensure_card
    CardWait --> Off: BDICP 或超时
    Off --> BeamWait: start_send
    BeamWait --> TxWait: 有效波束后发 CCTCQ
    TxWait --> Off: FKI 或超时 关PA关电
    Off --> PassThru: passthru_enter
    PassThru --> Off: exit / 保护
```

## 发信一拍

```mermaid
flowchart TD
  A[start_send] --> B[上电 PRDSS/LNA/PLNA]
  B --> C[等 PWI 有效波束]
  C --> D{数量大于等于1 且 S2C_d 大于 40?}
  D -->|否超时 30s| F[失败 mq 关电]
  D -->|是| E{cfg.pa_enable?}
  E -->|1| G[开 PA]
  E -->|0| H[CCTCQ]
  G --> H
  H --> I[等 FKI 15s]
  I --> J[关 PA 关电 mq]
```

透传不查卡。产品 SESSION 拍前无卡则本层不会被叫到。

## 使用

```json
{"id":18,"cmd":"test.rdss.card"}
{"id":19,"cmd":"test.rdss.send"}
{"cmd":"stream.set","name":"rdss","enable":1}
```

先 `cfg.get` 看 `bd_card`。`pa_enable` 默认 0。
