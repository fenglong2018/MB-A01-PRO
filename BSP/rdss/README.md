# RDSS

> **变更记录（2026-08-18）**  
> 本模块协议未改。`LOW_BATT` 走既有 `rdss_start_send` 发 1 条 N（含 OFF 冷启动 WARN）。告警拍不被低电打断。

北斗短报文（TD3050 / USART2 PB4/PB5 remap）。引脚见 `board/board_pins.h`。
协议见 `TD3050接口协议20241201.pdf`。

流程图与 CLI：[`flow.md`](flow.md)。

## 开关

`config/product_config.h` → `USE_RDSS=1`；`rtconfig.h` → `RT_USING_USART2`。

## 已锁定行为

| 项 | 结论 |
|----|------|
| 波特率 | 115200 |
| 有效波束 | 数量 ≥1；PWI 时间 >20；**S2C_d > 40** |
| 波束超时 | 30s；FKI 等待 15s |
| PA | 有效波束后再开；`cfg.pa_enable` 默认 **0** |
| 收信地址 | `cfg.recv_id` 默认 `13500001` |
| 本机卡号 | `$BDICP` 字段1 → `cfg_note_bd_card`（变了才进 Flash 4 槽）；**不改** `device_id` |
| 发成功 | 关 PA / PRDSS / LNA_RDSS + PLNA release |
| 无卡 | 产品 SESSION 不启 GNSS/RDSS（不开电）；拍中拔卡不中断；**透传不查卡**；透传时 CDC ulog 由 MODE 静音 |
| 电源极性 | PLNA/LNA_RDSS 高开；PRDSS/EN_5V 低开 |

## API

- `rdss_ensure_card(timeout_ms)` / `rdss_get_card_id()`
- `rdss_start_send(payload, len)` → 结果 `rdss_result_mq()`（上电亦发 CCICR）
- `rdss_passthru_*` / `rdss_on_bat_protect`

## 解耦

```text
BSP/key       SIM → sim_present()
services/cfg  身份 Flash；`$BDICP` → cfg_note_bd_card
BSP/rdss      电源 / PWI / PA / CCICR|BDICP / CCTCQ|FKI
app/session   有卡 → GNSS → ensure_card → msg_pack → RDSS；不校 RTC
```

## JSON 示例

```json
{"id":1,"cmd":"cfg.get"}
{"id":2,"cmd":"cfg.set","recv_id":13500001,"pa_enable":0}
{"id":3,"cmd":"stream.set","name":"rdss","enable":1}
```
