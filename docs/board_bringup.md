# 板级按模块验证

> **变更记录（2026-08-25）**  
> 串口助手开关 GNSS/RDSS 透传：第 0.3 节 + 第 7、8 节。  
> 透传：**GNSS/RDSS UART ↔ 同一 USB CDC**。进 `PASSTHRU` 后 CDC ulog 默认静音。  
> CLI `io.*` 已接真实 GPIO。第 12 节是调试现场勾选表。

关联：[`services/cli/README.md`](../services/cli/README.md)、[`tools/host_pc/README.md`](../tools/host_pc/README.md)、[`app/mode/fsm.md`](../app/mode/fsm.md)、[`docs/module_status.md`](module_status.md)。

---

## 0. 怎么做最好

1. **不要**为每个模块改 `product_config.h` 重编。除非某模块把系统卡死、USB 都没了，才临时 `USE_xxx 0` 缩小范围。  
2. **USB 一直插着。** 插着会进 `CHARGE`（或透传时 FSM 为 `PASSTHRU`、灯走充电外观），**不会进 STOP2**，COM 口不会被真关机掐掉。  
3. 入口：`tools/host_pc` → 选 COM → 连接（DTR=1）→ Ping。也可用任意串口助手（第 0.3 节）。  
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
| 无独立「晶振」命令 | — | HSE：USB 能枚举；LSE：`test.rtc` |
| 上位机无独立 NMEA 终端 | 开关+解析在「射频」页 | 表看 GSV/PWI；默认可不刷 `$` 到日志 |

退出透传：发一行以 `{` 开头的 JSON（如 `stream.set` `enable:0`）。透传中非 `{` 行会写给模块。CDC ulog 随退出恢复。要在透传里看固件日志：先 `{"cmd":"log.cdc","passthru_mute":0}`（仅 RAM，不进 `cfg`）。

### 0.3 串口助手开关透传

USB **虚拟串口（CDC）**，与 CLI / ulog / 透传共用同一个 COM。不要和 `shy_host` 同时占这个口。

| 项 | 设置 |
|----|------|
| 波特率 | 115200 8N1（CDC 实际不吃波特率） |
| DTR | **打开**（多数助手默认开；不开可能没应答） |
| 发送 | 一行一条，以 `\n` 或 `\r\n` 结尾 |
| SSCOM 快捷条 | 导入 [`tools/sscom/sscom_cli.ini`](../tools/sscom/sscom_cli.ini)；须勾「加回车换行」 |

先测通口，再看能否开透传：

```json
{"id":1,"cmd":"ping"}
{"id":2,"cmd":"mode.get"}
```

`ping` 应回 `ok:1`。`mode` 须为 `CHARGE`（插着 USB）或 `ON`。**`OFF` / `ALARM` / 保护不能开透传。**

开/关命令见第 7、8 节。建议 **一次只开 GNSS 或 RDSS 一个通道**。两个都开过，必须两个都 `enable:0` 才退出 `PASSTHRU`。

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

看 `mv` / `lookup_mv` / `pct` / `level` / `charge` / `vdda`（即 `adc_vdda_mv` 系数）。插 USB 时 `charge` 应为 1。万用表量电芯对照 `mv`；偏了改 `adc_vdda_mv`（默认 3300），不要靠内部 1.2V。充电查表另减 `charge_offset_mv`。

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

`io.list` / `io.get` / `io.set` 读写真实 GPIO（`val` 为脚电平）。产品轨仍由会话管，IO 页强拨会和引用计数打架：

- `MCU_EN_PLNA`：`pwr_plna_acquire`（GNSS/RDSS 共用）  
- `EN_PGNSS` / `EN_LNA_GNSS`：GNSS 自管  
- RDSS 使能在 `BSP/rdss`

CHARGE 下灯动画会覆盖 LED 写入。测轨仍可用第 9 步 `test.gnss.fix` / `test.rdss.card` 加表笔。

---

## 7. GNSS 透传

开：

```json
{"id":3,"cmd":"stream.set","name":"gnss","enable":1}
```

通过：应答 `ok:1`；`mode.get` 为 `PASSTHRU`；COM 上见 `$GNGGA` / `$GNRMC` 等 NMEA。ulog 默认静音。上位机走 **射频** 页可见星/SNR。

开透传后：

| PC 发出的行 | 去向 |
|-------------|------|
| 以 `{` 开头 | 仍是 JSON CLI（用来关透传） |
| 不以 `{` 开头 | 直接写到 GNSS UART |

关（**必须**发 `{` 开头，漏 `\n` 也不会处理）：

```json
{"id":6,"cmd":"stream.set","name":"gnss","enable":0}
```

不关则后续 `test.gnss.fix` 会 `busy_passthru`。

---

## 8. RDSS 透传

开：

```json
{"id":4,"cmd":"stream.set","name":"rdss","enable":1}
```

通过：应答 `ok:1`；`mode.get` 为 `PASSTHRU`；COM 上见 `$BD…`（如 `$BDPWI`）。ulog 默认静音。上位机射频页看波束/S2C。建议先关 GNSS 再开 RDSS（一次一个通道）。

PC 发的非 `{` 行写到 RDSS UART。关：

```json
{"id":7,"cmd":"stream.set","name":"rdss","enable":0}
```

查询：`{"cmd":"stream.get","name":"rdss"}` 或 `{"cmd":"stream.list"}`。

常见失败：`not_built`（未编 `USE_RDSS`）；`mode.get` 仍不是 `PASSTHRU`（当前 OFF/ALARM/保护，或模块 `enter` 失败，量轨）。

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

先查卡（`cfg.get` 的 `bd_card`），再发载荷 `TEST`。要波束、卡、频度允许。**本版** 5V 随 RDSS 上电，不看 `pa_enable`（该字段默认 0 也照样开 PA）。TD3203B 规格：5V 只供发射功放，接收/查卡不需要；本版测完后再改「仅发射开 PA」。

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
