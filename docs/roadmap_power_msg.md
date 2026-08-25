# 电源 / 模式 / 报文 — 规划（含未实现项）

本文记录已确认规则与后续工作。

> **变更记录（2026-08-18）**  
> STOP2 / RTC 10s / 硬件 IWDG **已接代码**。`LOW_BATT` 扩展到 OFF/看电；冷启动 WARN 发 1 条，BKP `lb_sent` 防 STOP2 重复发。告警不插 N。FAKE_OFF 10s 空载采 ADC。板级电流仍待测。

---

## 1. 硬件与 ADC（本迭代实现）

### 分压

```text
BAT -- R16(1.2M) --+-- AD_BAT (PA4 / ADC2 CH1)
                   |
                 R17(3.3M)
                   |
                  GND
```

- \(V_{AD} = V_{BAT} \times 3.3 / 4.5 = V_{BAT} \times 11/15\)
- \(V_{BAT} = V_{AD} \times 15/11\)
- 满电 4.3V 时 \(V_{AD}\approx 3.15V\)（VDDA≈3.3V 安全）
- 分压等效源阻约 **880kΩ** → 必须用最长采样时间（`ADC_SAMP_TIME_239CYCLES5`）

### 校准与 VDDA

- 上电：`ADC_StartCalibration`（芯片自校准）
- 采内部 `ADC_CH_INT_VREF`（CH18，标称 **1.2V**）算实际 VDDA  
  `VDDA_mV = 1200 × 4095 / raw_vref`
- 再用 **ADC1 VREFINT** 算 VDDA，**ADC2 CH1（PA4）** 码值还原电芯电压

### 采集节奏

- 上电：自校准 + **空载采 1 次**
- 之后：**通知采集**。MODE：`BATT` / 进 `CHARGE` / `FAKE_OFF` RTC 10s；SESSION：开 GNSS 前
- 拔 USB、透传、`OFF`/`FORCE_OFF`：**不采**（CHARGE 退出后第一拍 SESSION 用缓存）

### 充电补偿

- 同一张 OCV 表；充电且 \(V&lt;4.18\mathrm{V}\)：`V_lookup = V_meas - charge_offset_mv`（默认 100mV，JSON `cfg.charge_offset_mv`）
- \(V\ge 4.18\mathrm{V}\) 不再减 offset

### DMA？

| 方案 | 结论 |
|------|------|
| `ADC1_DMA` 连续扫 | **不做**。事件触发、两通道软件转换足够 |
| RT-Thread `drv_adc` 注册 | **不用**。未开 `RT_USING_ADC`；产品侧自管 ADC1 |
| 独立任务 | **要**。阻塞等通知，不空转 |

实现目录：`BSP/adc/`（`adc_bat.c/h`）。

### 电量架构（OCV 查表 → 百分比）

```text
ADC码值 → Vbat(mV) → OCV表[101] → percent(0..100) → level → MODE
表：adc_bat_soc.h；锚点见该文件（0%=3.00V … 100%=4.20V）
```

| 门限 | 进入 | 恢复（滞回） | 约等于 |
|------|------|--------------|--------|
| 保护 | &lt; **7%** | ≥ **9%** | 3.3V / 3.4V |
| 低电预警 | &lt; **12%** | ≥ **17%** | 3.5V / 3.6V |

上层只读 `adc_bat_get_percent()` / `adc_bat_get_level()`。

**上位机：** MBA01 电量仍 2 位 `00`～`99`，协议不用改。`tools/host_pc` V0.2 可改 `charge_offset_mv`。详见 `BSP/adc/README.md`、`tools/host_pc/README.md`。

---

## 2. 模式扩展

| 状态 | 含义 | 状态 |
|------|------|------|
| `FORCE_OFF` | 低压强制关机；仅 USB→`CHARGE` 解除 | **已实现**（见 `fsm.md`） |
| `LOW_BATT` | 低电量告警；只发一条短报文 | **已实现**（ON / FAKE_OFF←ON / OFF / 关机看电；告警不进；`lb_sent`） |
| `FAKE_OFF` | 假关机（发完一拍；resume=ON/ALARM） | **已实现**（STOP0 + RTC 10s 闪灯/采电） |

### `FORCE_OFF` 行为（已实现）

1. ADC `PROTECT` 边沿 → `MODE_EVT_BAT_PROTECT` → `FORCE_OFF`（`CHARGE` 中忽略）
2. `FORCE_OFF`/`OFF` 短按可进 `BATT` 看电；**禁止 `BATT→ON`**
3. 保护 / `FORCE_OFF` 下 SOS 长按、FALL **不进 ALARM**
4. USB 插入 → `CHARGE`（prev=`OFF`）；拔出后若仍保护再回 `FORCE_OFF`

---

## 3. 报文调度（已实现）

| 场景 | 规则 |
|------|------|
| `LOW_BATT` | 进入后发 **1 条** 短报文 |
| `ALARM`（应急） | 进告警 **立刻** 1 次；**0～24h / 2min**；**24～48h / 5min**；**48h 起 / 10min**；满 **72h 仍停 ALARM**（报文 A；墙钟 Unix；未校时一直 2min） |
| `ON` | 每 **10 分钟** 定位 + 常态报文 |

实现：`app/session/session_alarm.*` + `msg_pack.*`。无 SIM 本拍空过；透传 / `test.*` 不查卡。

---

## 4. 低功耗 STOP2 + 假关机

完整方案：**[`docs/low_power_stop2.md`](low_power_stop2.md)**

| 要点 | 内容 |
|------|------|
| 逻辑 `FAKE_OFF` | **已接**：一拍结束进假关机；`SHOT_BUSY` 醒跑 GNSS/RDSS |
| idle STOP0 | **已接** |
| RTC 10s 浅醒 | **已接**（闪灯 + FAKE_OFF 空载采电） |
| STOP2 真关机 | **已接代码**（OFF/FORCE_OFF；醒后复位） |
| 硬件 IWDG | **已接**；非 OFF/FORCE_OFF 开，超时 ≈26s |

---

## 5. 实施顺序

| 阶段 | 内容 | 状态 |
|------|------|------|
| A | `BSP/adc` 采集 / 校准 / 滤波 / level API + 任务 | **完成** |
| B | MODE：`FORCE_OFF`、保护拦截、`BATT` 禁开机 | **完成** |
| C | `LOW_BATT` 状态 + 单次短报文 | **完成** |
| D | `ALARM` 2/5/10min + `ON` 10min（满 72h 不切 ON） | **完成** |
| D3 | GNSS RMC → RTC（ON 一次；ALARM 仅未校时） | **完成** |
| D4 | 逻辑 `FAKE_OFF` + 告警短按看电 | **完成** |
| E | BATT/LED 电量显示接 ADC 百分比 | **完成**（ON 100ms / ALARM 双闪） |
| F | `BSP/gnss` 正常定位 / 透传 / 结果 mq | **完成** |
| G | RDSS 真实发信 | **基本完成**（`$BDICP` → `cfg_note_bd_card`） |
| H0 | 低功耗：idle hook + STOP0（`BSP/pm`） | **完成** |
| H1 | RTC 10s 浅醒 + FAKE_OFF 采电 | **完成**（2026-08-18） |
| H | STOP2：真 OFF/FORCE_OFF + IWDG | **代码完成**（2026-08-18）；电流待测 |
| H2 | 冷启动 OFF+WARN 发 1 条 N + `lb_sent` | **完成**（2026-08-18） |
| 调试 | 透传 USB 桥（`stream_set_sink` + CDC 下行） | **已接**；与 CLI 同一 COM |
| 调试 | 透传时 CDC ulog 静音 | **已接**（MODE hold；`log.cdc` RAM 覆盖，不进 cfg） |
| 调试 | CLI `io.*` 真实 GPIO | **未接**（`not_ready`） |

---

## 6. 相关文件

- 现行状态机：`app/mode/fsm.md`
- 三套库：`BSP/nvflash/README.md`
- 本规划：`docs/roadmap_power_msg.md`
- 低功耗方案：`docs/low_power_stop2.md`
- RTC / GNSS 校时：`BSP/rtc/README.md`、`BSP/gnss/README.md`
- ADC：`BSP/adc/`
