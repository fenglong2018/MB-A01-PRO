# RDSS

北斗短报文（TD3050 / USART2 PB4/PB5 remap）。引脚见 `board/board_pins.h`。
协议见 `TD3050接口协议20241201.pdf`。

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
| 本机卡号 | `$CCICR,0,00*68` → `$BDICP` **字段1 用户地址(ID)**；缓存并同步 `cfg.device_id` |
| 发成功 | 关 PA / PRDSS / LNA_RDSS + PLNA release |
| 无卡 | 不启会话发信；仅日志（KEY 双沿滤波） |
| 电源极性 | PLNA/LNA_RDSS 高开；PRDSS/EN_5V 低开 |

## API

- `rdss_ensure_card(timeout_ms)` / `rdss_get_card_id()`
- `rdss_start_send(payload, len)` → 结果 `rdss_result_mq()`（上电亦发 CCICR）
- `rdss_passthru_*` / `rdss_on_bat_protect`

## 解耦

```text
BSP/key       SIM → sim_present()
services/cfg  recv_id / pa_enable / device_id + JSON cfg.*
BSP/rdss      电源 / PWI / PA / CCICR|BDICP / CCTCQ|FKI
app/session   有卡 → GNSS → ensure_card → msg_pack → RDSS
```

## JSON 示例

```json
{"id":1,"cmd":"cfg.get"}
{"id":2,"cmd":"cfg.set","recv_id":13500001,"pa_enable":0}
{"id":3,"cmd":"stream.set","name":"rdss","enable":1}
```
