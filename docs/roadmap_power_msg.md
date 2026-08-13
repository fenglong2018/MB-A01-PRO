# 电源 / 模式 / 报文 — 规划（含未实现项）

本文记录已确认规则与后续工作。`FORCE_OFF` / `LOW_BATT` / ALARM·ON 报文节奏已接入；假关机预留空。

---

## 1. 硬件与 ADC（本迭代实现）

### 分压

```text
BAT -- R16(1.2M) --+-- AD_BAT (PA4 / ADC_CH_4)
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
- 再用 VDDA 与 AD_BAT 码值算电芯电压

### DMA？

| 方案 | 结论 |
|------|------|
| `ADC1_DMA` 连续扫 | **本期不做**。只需 2 通道、约 1Hz 周期采样，软件触发单次转换足够 |
| RT-Thread `drv_adc` 注册 | **本期不用**。未开 `RT_USING_ADC`；产品侧自管 ADC1，与 KEY 风格一致 |
| 独立任务 | **要**。低优先级线程周期采集，不阻塞 MODE/KEY |

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

---

## 2. 模式扩展

| 状态 | 含义 | 状态 |
|------|------|------|
| `FORCE_OFF` | 低压强制关机；仅 USB→`CHARGE` 解除 | **已实现**（见 `fsm.md`） |
| `LOW_BATT` | 低电量告警；只发一条短报文 | **已实现**（ON + WARN 边沿） |
| `SLEEP` | 假关机 | **预留空，不实施** |

### `FORCE_OFF` 行为（已实现）

1. ADC `PROTECT` 边沿 → `MODE_EVT_BAT_PROTECT` → `FORCE_OFF`（`CHARGE` 中忽略）
2. `FORCE_OFF`/`OFF` 短按可进 `BATT` 看电；**禁止 `BATT→ON`**
3. 保护 / `FORCE_OFF` 下 SOS 长按、FALL **不进 ALARM**
4. USB 插入 → `CHARGE`（prev=`OFF`）；拔出后若仍保护再回 `FORCE_OFF`

---

## 3. 报文调度（后续，先框架/空实现）

| 场景 | 规则 |
|------|------|
| `LOW_BATT` | 进入后发 **1 条** 短报文 |
| `ALARM`（应急） | 进告警 **立刻** 定位/发信 1 次；**0～24h 每 2 分钟**；**24～48h 每 5 分钟**；满 **48h** 切回 **`ON`** |
| `ON` | 每 **10 分钟** 定位 + 常态报文 |

GNSS 细则与状态图：`BSP/gnss/README.md`。

建议落点：

```text
app/session/session_msg.h/.c     # 调度框架（定时器 + 回调空实现）
app/session/session_alarm.*      # 已有告警会话启停；报文节奏挂这里或 msg
BSP/rdss / services              # 真正组包发送（后接）
```

第一期：接口 + 日志 `[MSG] stub ...`，不发空中报文。

---

## 4. 低功耗 STOP2 + 假关机（方案已记录，未实施）

完整方案：**[`docs/low_power_stop2.md`](low_power_stop2.md)**

| 要点 | 内容 |
|------|------|
| 休眠 | MCU **STOP2**；只保 MODE + 告警时间轴 + RTC/配置 |
| 假关机 | 新状态 **`SLEEP`**（勿与真 `OFF` 合并）；ALARM/ON 发完进入 |
| 唤醒 | RTC Alarm + KEY/USB EXTI；醒后冷恢复/软件复位再按态重跑 |
| 现状 | **仅文档**；代码未动 |

---

## 5. 实施顺序

| 阶段 | 内容 | 状态 |
|------|------|------|
| A | `BSP/adc` 采集 / 校准 / 滤波 / level API + 任务 | **完成** |
| B | MODE：`FORCE_OFF`、保护拦截、`BATT` 禁开机 | **完成** |
| C | `LOW_BATT` 状态 + 单次短报文 | **完成** |
| D | `ALARM` 2/5min + `ON` 10min 会话 | **完成**（锚点仍为 tick；假关机不做） |
| E | BATT/LED 电量显示接 ADC 百分比 | **完成**（`BSP/led`） |
| F | `BSP/gnss` 正常定位 / 透传 / 结果 mq | **完成** |
| G | RDSS 真实发信 | **基本完成** |
| H0 | 低功耗：idle hook + STOP0（`BSP/pm`） | **完成**（浅睡；SysTick 仍约 10ms 醒） |
| H | 低功耗：`SLEEP` + RTC Alarm + STOP2 | **方案已写**，待实施 |

---

## 6. 相关文件

- 现行状态机：`app/mode/fsm.md`
- 本规划：`docs/roadmap_power_msg.md`
- 低功耗方案：`docs/low_power_stop2.md`
- ADC：`BSP/adc/`
