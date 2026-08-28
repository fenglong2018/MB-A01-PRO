# BSP/adc — 电池电量

> **变更记录（2026-08-28）**  
> 分压改为 **R16=390k、R17=1.1M**（约 2.8μA @ 4.2V），还原 `Vbat = Vpin × 149/110`。满量程用 `cfg.adc_vdda_mv`（默认 3300）。表笔 4.17V 与软件对齐。

> **变更记录（2026-08-24）**  
> PA4 改走 **ADC2 CH1**（N32WB452：ADC1 CH4 是 PA3）。  
> 采集表增加：`FAKE_OFF` RTC 10s 空载 `request()` 1 次（不开 15s）。CHARGE 15s、拔 USB 不采、会话前 `sample_wait` 不变。  
> 边沿：WARN→`BAT_WARN`；PROTECT→`BAT_PROTECT`；回到 OK→`BAT_OK`（MODE 清 `lb_sent`）。  
> MODE 受理 WARN：ON / `FAKE_OFF←ON` / OFF / 关机看电；告警不进 `LOW_BATT`。

## 文件分工

流程图：[`flow.md`](flow.md)。用法：`{"cmd":"test.adc"}`。

| 文件 | 内容 |
|------|------|
| `adc_bat.h` | 宏：百分比档、保护/预警、充电压差；API |
| `adc_bat_soc.h` | `adc_bat_ocv_mv[101]`（const，非宏）；仅 `adc_bat.c` 用 |
| `adc_bat.c` | 通知采集、充电 offset、查表、level |

## 采集时机

上电校准后空载采 **1 次**。之后线程阻塞，被通知才采：

| 来源 | 时机 |
|------|------|
| MODE | 进 `BATT`：`request()` |
| MODE | 进 `CHARGE`：`set_charging(1)`（立刻采 + 15s 周期） |
| MODE | 离 `CHARGE`（拔 USB）：`set_charging(0)`（停周期，**不采**） |
| MODE | 进 `PASSTHRU`：`pause()`（停周期，不采） |
| MODE | `FAKE_OFF` 的 RTC 10s：`request()` 空载 1 次（不开 15s 周期） |
| SESSION | 开 GNSS 前 `sample_wait`；刚离开 CHARGE 的第一拍跳过 |

GNSS/RDSS 工作中、透传中、KEY/LED **不采**。  
`OFF` / `FORCE_OFF`：无周期、无采集（STOP2）。冷启动那 1 次空载采样若已是 WARN，由 MODE 决定是否发 1 条 N（BKP `lb_sent` 防 STOP2 重复）。

## 满量程校准（不用内部 1.2V）

```text
vpin = adc_vdda_mv × raw_bat / 4095
vbat = vpin × 15/11（对照用；板子已是 390k+1.1M）
```

`adc_vdda_mv` 默认 **3300**，当 ADC 满量程（VDDA）。表笔比 `test.adc` 的 `mv` 高就**加大**这个数，低就减小。  
例：软件 4100、表笔 4200 → `3300 × 4200 / 4100 ≈ 3380`，再 `cfg.set`。范围 2500～4000，进 Flash。`test.adc` 的 `vdda` 即当前系数。

## 充电查表

仍用同一张 OCV 表。充电且端电压 &lt; 4.18V：

`V_lookup = V_meas - charge_offset_mv`（cfg，默认 100mV，0=关）

≥4.18V 不再减。`vbat_mv` 为真实端电压；percent/level 用 `v_lookup_mv`。

标定：空载读 `test.adc` 的 `mv`，插 USB 约 2s 再读，差值写入 `cfg.charge_offset_mv`（或宏 `ADC_BAT_CHARGE_OFFSET_MV`）。

## 对外影响（报文 / 上位机）

| 通道 | 是否改协议 | 说明 |
|------|------------|------|
| MBA01 短报文 | **否** | 电量仍为头里 2 位 ASCII `00`～`99`；只是采样更贴近空载，充电虚高不进报文（CHARGE 本不发信） |
| USB CLI / `tools/host_pc` | **加字段，旧端可忽略** | `cfg.*` 增 `charge_offset_mv`；`test.adc` 增 `lookup_mv`/`charge`/`offset_mv`。V0.2 配置页可改偏移 |

配置页改充电偏移见 `tools/host_pc/README.md`（V0.2）。

## OCV 锚点（产品给定）

| % | V | % | V |
|---|---|---|---|
| 100 | 4.20 | 50 | 3.82 |
| 90 | 4.06 | 40 | 3.79 |
| 80 | 3.98 | 30 | 3.73 |
| 70 | 3.92 | 20 | 3.68 |
| 60 | 3.87 | 10 | 3.45 |
| | | 0 | 3.00 |

段内线性插成 0..100 共 101 档；&lt;3.00V→0%，&gt;4.20V→100%。

## 门限（按表反查电压）

| 门限 | 百分比 | 约电压 |
|------|--------|--------|
| 保护进入 / 恢复 | &lt;7% / ≥9% | 3.3V / 3.4V |
| 预警进入 / 恢复 | &lt;12% / ≥17% | 3.5V / 3.6V |
