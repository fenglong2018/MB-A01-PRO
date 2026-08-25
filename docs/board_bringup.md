# 板级按模块验证

> **变更记录（2026-08-19）**  
> 透传：**GNSS/RDSS UART ↔ 同一 USB CDC**。进 `PASSTHRU` 后 CDC ulog 默认静音。  
> CLI `io.*` 仍 `not_ready`。第 12 节是调试现场勾选表。

关联：[`services/cli/README.md`](../services/cli/README.md)、[`tools/host_pc/README.md`](../tools/host_pc/README.md)、[`app/mode/fsm.md`](../app/mode/fsm.md)、[`docs/module_status.md`](module_status.md)。

---

## 0. 怎么做最好

1. **不要**为每个模块改 `product_config.h` 重编。除非某模块把系统卡死、USB 都没了，才临时 `USE_xxx 0` 缩小范围。  
2. **USB 一直插着。** 插着会进 `CHARGE`（或透传时 FSM 为 `PASSTHRU`、灯走充电外观），**不会进 STOP2**，COM 口不会被真关机掐掉。  
3. 入口：`tools/host_pc` → 选 COM → 连接（DTR=1）→ Ping。也可用任意串口助手，一行一条 JSON，`\n` 结尾。  
4. 一次只动一个变量。`test.gnss.fix` / `test.rdss.*` / `test.session.once` 会阻塞，等 RSP 再发下一条。  
5. 射频步骤同时看 **轨电压**，不要只看 JSON。

建议顺序：晶振 → USB → KEY → ADC → LED → 电源轨 → GNSS 透传 → RDSS 透传 → GNSS 定位 → RDSS 发信 → 一拍会话 → 再拔 USB 测 MODE/STOP2。

### 0.1 本流程软件已具备

| 步 | 命令 / 现象 |
|----|-------------|
| USB | `ping`、`mode.get`、ulog 上 CDC |
| LSE/RTC | `test.rtc` |
| KEY | `test.key` |
| ADC | `test.adc` |
| LED | `test.led` |
| GNSS 定位 | `test.gnss.fix`（不写 RTC） |
| RDSS | `test.rdss.card` / `test.rdss.send` |
| 一拍 | `test.session.once`（不改 MODE） |
| 透传 | `stream.set` → `PASSTHRU`；COM 上见模块句；ulog 默认静音 |

### 0.2 仍未接完

| 缺口 | 影响 | 上板替代 |
|------|------|----------|
| **`io.*` 全 `ready:0`** | 上位机 IO 页不能拨 EN_* / LED | 会话时万用表量轨；灯用 `test.led` |
| 无独立「晶振」命令 | — | HSE：USB 能枚举；LSE：`test.rtc` |
| 上位机无独立 NMEA 终端 | 开关+解析在「射频」页 | 表看 GSV/PWI；默认可不刷 `$` 到日志 |

退出透传：发一行以 `{` 开头的 JSON（如 `stream.set` `enable:0`）。透传中非 `{` 行会写给模块。CDC ulog 随退出恢复。要在透传里看固件日志：先 `{"cmd":"log.cdc","passthru_mute":0}`（仅 RAM，不进 `cfg`）。

整机（假关机 10s、STOP2、72h、低电 N）第 11 步以后 **拔 USB** 测。

---

## 1. 晶振 / 时钟

板子：**HSE 8MHz × 18 → PLL 144MHz**（Nations 评估板 / 官方 USB 例程；`Makefile` 里 `HSE_VALUE`）。RTC：**LSE 32.768kHz**。若焊盘是 32M 晶振，把 `HSE_VALUE` 改成 `32000000`。

| 钟 | 怎么验 | 通过 |
|----|--------|------|
| HSE | USB 能枚举、Ping `ok:1` | PLL 起来；起不来通常枚举失败 |
| LSE | `{"id":21,"cmd":"test.rtc"}` | 有 `unix`/`synced`；ulog 有 `[RTC]`。未校时 `unix=0`、`synced=0` 也算硬件活着 |
| LSE 走时 | `{"id":22,"cmd":"test.rtc","unix":1735689600}`，等 10s 再读 | unix 大约 +10 |

有示波器可看 HSE/LSE 脚。没有则：**USB 通 ≈ HSE 通；`test.rtc` 能读写 ≈ LSE 通。**

---

## 2. USB 转串口

1. `make flash`，插 USB，出现 CDC COM。  
2. 上位机连接 → `{"id":1,"cmd":"ping"}` → `ok:1`。  
3. `{"id":13,"cmd":"mode.get"}`：插电应为 `CHARGE`。  
4. 日志区能刷 ulog（需 DTR）。

失败：驱动、线、DTR、口被占用。

---

## 3. ADC

```json
{"id":15,"cmd":"test.adc"}
```

看 `mv` / `lookup_mv` / `pct` / `level` / `charge`。插 USB 时 `charge` 应为 1。万用表量电芯对照 `mv`（充电查表会减 `charge_offset_mv`，默认 100mV）。

---

## 4. 电量指示（LED）

LED1/2/3 **低电平点亮**。

```json
{"id":16,"cmd":"test.led","mode":4,"pct":50}
```

`mode`：`0=OFF` 灭，`2=ON` 三灯约 10s 闪 100ms，`3=ALARM` 双闪，`4=CHARGE` 流水。`pct` 管流水档（&lt;30 / &lt;70 / ≥70）；`pct≥98` 充电应三灯常亮。

插 USB 本身就会走 CHARGE 流水。再 `test.adc` 对百分比。

---

## 5. KEY

```json
{"id":14,"cmd":"test.key"}
```

按 SOS / FALL、插拔 USB，看电平与 `sim_present`。`mode.get`：短按看电约 5s（灯流水一轮）。**先不要长按进 ALARM**，避免和模块测试缠在一起。

---

## 6. 控制输出 IO

`io.list` / `io.set` **会 `not_ready`**。产品轨：

- `MCU_EN_PLNA`：`pwr_plna_acquire`（GNSS/RDSS 共用）  
- `EN_PGNSS` / `EN_LNA_GNSS`：GNSS 自管  
- RDSS 使能在 `BSP/rdss`

**替代：** 跑第 9 步 `test.gnss.fix` 时量 GNSS 轨是否先高后低；`test.rdss.card` 时量 RDSS/PLNA。不要用 CLI 强拨，以免和引用计数打架。

---

## 7. GNSS 透传

```json
{"id":3,"cmd":"stream.set","name":"gnss","enable":1}
```

通过：`mode.get` 为 `PASSTHRU`；上位机 **射频** 页可见星/SNR。PC 发的非 JSON 行会到 GNSS UART。ulog 默认静音。

测完：`{"cmd":"stream.set","name":"gnss","enable":0}`。不关则后续 `test.gnss.fix` 会 `busy_passthru`。

---

## 8. RDSS 透传

`stream.set` `name":"rdss"`。一次只开一个通道。通过：射频页 `$BDPWI` 波束/S2C。测完关掉。

---

## 9. GNSS 定位（正式路径）

透传关掉后：

```json
{"id":17,"cmd":"test.gnss.fix"}
```

成功：有效定位 + `unix`（RMC；**本命令不写 RTC**）。可加大 `wait_ms`。同时量 `EN_PGNSS`/LNA。

产品校时是 ON 第一次有效定位，不是这条 test。

---

## 10. RDSS

```json
{"id":18,"cmd":"test.rdss.card"}
{"id":19,"cmd":"test.rdss.send"}
```

先查卡（`cfg.get` 的 `bd_card`），再发载荷 `TEST`。要波束、卡、频度允许。`pa_enable` 默认 0。

---

## 11. 之后（整机，可拔 USB）

| 步 | 命令 / 操作 | 目的 |
|----|-------------|------|
| 一拍会话 | `test.session.once` | GNSS+打包+RDSS，不改 MODE |
| 配置 | `cfg.get` / `cfg.set`，掉电再读 | Flash 身份 |
| 校时 | ON 下等第一次定位，再 `test.rtc` 看 `synced` | 只 GNSS 校 |
| 假关机 10s | **拔 USB**，发完一拍后灯 10s 闪 | RTC WakeUp |
| STOP2 | OFF 后电流；KEY/USB 醒 | 醒后软件复位，COM 会断再枚举 |
| 低电 N | 电量到预警 | 发 1 条 N；告警不插 |

---

## 12. 调试勾选表（USB 插着）

上板调试时按项打勾，确认该模块过了再往下。**不是「还没写的软件清单」。**

- [ ] Ping  
- [ ] `test.rtc`  
- [ ] `test.key`  
- [ ] `test.adc`  
- [ ] `test.led`  
- [ ] `stream.set` gnss → COM 上见 NMEA → 关  
- [ ] `test.gnss.fix`  
- [ ] `test.rdss.card`  
- [ ] `test.rdss.send`  
- [ ] `test.session.once`  

射频失败：先量轨和 UART，再查超时/卡号。
