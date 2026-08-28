# RDSS

> **变更记录（2026-08-18）**  
> 本模块协议未改。`LOW_BATT` 走既有 `rdss_start_send` 发 1 条 N（含 OFF 冷启动 WARN）。告警拍不被低电打断。
>
> **备注（2026-08-27，本版不改代码）**  
> 板载模块为 TD3203B（规格书 V1.2）。`EN_5V` 对应模块 **VCC_PA（5V）**，只给发射功放；接收/查卡走 **VCC 3.3V**（`EN_PRDSS`）。规格：待机单接收 ≤0.65W，发射瞬态 ≤6.5W。**本版仍是 RDSS 上电即开 5V**（含查卡、收信、透传）。下一版拟改为仅 CCTCQ/发射窗口开 PA，发前留稳定时间；`pa_enable` 届时再定是否重新参与。

北斗短报文（TD3203B；协议文档仍用 TD3050 / USART2 PB4/PB5 remap）。引脚见 `board/board_pins.h`。
协议见 `TD3050接口协议20241201.pdf`。电气见 `TD3203B北斗三短报文模块规格书V1.2.pdf`。

流程图与 CLI：[`flow.md`](flow.md)。

## 开关

`config/product_config.h` → `USE_RDSS=1`；`rtconfig.h` → `RT_USING_USART2`。

## 已锁定行为

| 项 | 结论 |
|----|------|
| 波特率 | 115200 |
| 有效波束 | 数量 ≥1；PWI 时间 >20；**S2C_d > 40** |
| 波束超时 | 30s；FKI 等待 15s |
| 5V PA | **本版：RDSS 上电即开**（`EN_5V` 低有效）；关 RDSS 时关。不看 `pa_enable`。规格上 5V 只供发射 PA，接收不必开；见上方 2026-08-27 备注 |
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

串口助手开/关透传：[`docs/board_bringup.md`](../../docs/board_bringup.md) 第 0.3、8 节。

```json
{"id":1,"cmd":"cfg.get"}
{"id":2,"cmd":"cfg.set","recv_id":13500001,"pa_enable":0}
{"id":3,"cmd":"stream.set","name":"rdss","enable":1}
{"id":7,"cmd":"stream.set","name":"rdss","enable":0}
```
