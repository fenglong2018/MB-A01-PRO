# services/cfg

身份与产品配置。只通过 `nvflash` 落盘；**不写 BKP、不碰 MODE**。

流程图：[`flow.md`](flow.md)。

> **变更记录（2026-08-19）**  
> `cfg.get` 导出 `hw_ver` / `sw_ver` / `upgrade_unix`。`hw_ver` 可出厂写入。

## 字段

| 字段 | RAM | Flash | 谁改 |
|------|-----|-------|------|
| `recv_id` | 是 | 是 | `cfg.set` |
| `device_id` 设备编号 | 是 | 是 | `cfg.set` / 出厂；**禁止 `$BDICP`** |
| `charge_offset_mv` | 是 | 是 | `cfg.set` |
| `adc_vdda_mv` | 是 | 是 | `cfg.set`，ADC 满量程校准默认 3300；旧槽 0 当 3300 |
| `hw_ver` | 是 | 是 | 出厂 `cfg.set`；空则空串 |
| `sw_ver` | 是 | 是 | 编译宏 `CFG_SW_VER`（现 `v0.2`），上电与 Flash 不一致则更新 |
| `upgrade_unix` | 是 | 是 | OTA 后补；现为 0，**不**走 `cfg.set` |
| 北斗卡 4 槽 | 是 | 号变才写 | `cfg_note_bd_card` |
| 4G ICCID 4 槽 | 预留全 0 | 预留 | 无 4G 协议 |
| `first_fix_unix` | 是 | 是 | session：GNSS RMC 第一次非 0，不读 RTC |
| `pa_enable` | 是 | **否** | `cfg.set`，重启回 0；**本版不挡 5V**（RDSS 上电即开 PA）。TD3203B：5V 只供发射；下一版再试发射窗口才开，见 `BSP/rdss/README.md` |

## 任务

上电 `INIT_ENV`：**同步** `nvflash_load`。  
`cfgnv` 等 `SAVE` 事件再擦写。CRC 失败整槽丢弃，两槽都坏用宏默认。

JSON `cfg.get` 含 `pa_enable`、`adc_vdda_mv`、`bd_card`、`first_fix_unix`、`hw_ver`、`sw_ver`、`upgrade_unix`。细则与复位流程：`BSP/nvflash/README.md`。

透传 CDC 日志静音 **不是** cfg 字段（调试 RAM，见 `services/log/README.md`）。
