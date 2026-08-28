# 功能模块完成度 / 文档 / 流程图

> **日期：2026-08-19**  
> 对照代码（非「规划里写过就算完成」）。用法走各模块 README + CLI；流程图走 `flow.md` / `fsm.md`。

上板步骤：[`board_bringup.md`](board_bringup.md)。整机状态机：[`app/mode/fsm.md`](../app/mode/fsm.md)。CLI：[`services/cli/README.md`](../services/cli/README.md)。

---

## 1. 完成度总表

| 模块 | 产品代码 | 缺口 | 用法 | 流程图 |
|------|----------|------|------|--------|
| `app/mode` | **完成** | 无（产品规则） | [README](../app/mode/README.md) | [fsm.md](../app/mode/fsm.md) |
| `app/session` | **完成** | 无 | [README](../app/session/README.md) | [flow.md](../app/session/flow.md) |
| `BSP/gnss` | **完成** | 无 | [README](../BSP/gnss/README.md) + `test.gnss.fix` | [flow.md](../BSP/gnss/flow.md) |
| `BSP/rdss` | **完成** | 板测波束/卡/PA；**本版 5V 上电即开**，规格上接收不必开（待测完再改） | [README](../BSP/rdss/README.md) + `test.rdss.*` | [flow.md](../BSP/rdss/flow.md) |
| `BSP/adc` | **完成** | 无 | [README](../BSP/adc/README.md) + `test.adc` | [flow.md](../BSP/adc/flow.md) |
| `BSP/led` | **完成** | 无 | [README](../BSP/led/README.md) + `test.led` | [flow.md](../BSP/led/flow.md) |
| `BSP/key` | **完成** | 无 | [README](../BSP/key/README.md) + `test.key` | [flow.md](../BSP/key/flow.md) |
| `BSP/rtc` | **完成** | 无 | [README](../BSP/rtc/README.md) + `test.rtc` | [flow.md](../BSP/rtc/flow.md) |
| `BSP/pm` | **完成** | STOP2 **电流待测** | [README](../BSP/pm/README.md) + `test.pm` | [flow.md](../BSP/pm/flow.md) |
| `BSP/iwdg` | **完成** | 实机确认 STOP 中狗 | [README](../BSP/iwdg/README.md) | [flow.md](../BSP/iwdg/flow.md) |
| `BSP/pwr` | **完成** | 无 | [README](../BSP/pwr/README.md) | [flow.md](../BSP/pwr/flow.md) |
| `BSP/nvflash` | **完成** | 无 | [README](../BSP/nvflash/README.md) | [flow.md](../BSP/nvflash/flow.md) |
| `BSP/bkp_user` | **完成** | 无 | [README](../BSP/bkp_user/README.md) | 同上三套库图 |
| `BSP/rram` | **完成**（STOP2 栈） | 业务 CRC 块无写入 | [README](../BSP/rram/README.md) | 同上 |
| `services/cfg` | **完成** | 无 | [README](../services/cfg/README.md) + `cfg.get/set` | [flow.md](../services/cfg/flow.md) |
| `services/stream` | **完成** | 无独立透传窗（上位机） | [README](../services/stream/README.md) + `stream.set` | [flow.md](../services/stream/flow.md) |
| `services/cli` | **完成**（控制面） | CHARGE 下 LED 动画会盖掉 `io.set` | [README](../services/cli/README.md) | [flow.md](../services/cli/flow.md) |
| `services/log` | **完成** | 无 | [README](../services/log/README.md) + `log.cdc` | [flow.md](../services/log/flow.md) |
| `services/usb` | **完成** | 单 CDC | [README](../services/usb/README.md) | [flow.md](../services/usb/flow.md) |
| `tools/host_pc` | V0.2 **可用** | 无独立原始 NMEA 终端（射频页已解析） | [README](../tools/host_pc/README.md) | 见 CLI/USB 图 |

产品 MODE / 报文 / 校时 / 透传 USB / 透传静音 ulog / STOP2 代码：**齐**。  
未做且明确不在本表当「缺功能」：双 CDC、CHARGE 15s 改 10s（未拍板）。

---

## 2. 仍未接 / 待板测（不是忘写的产品逻辑）

| 项 | 影响 | 怎么办 |
|----|------|--------|
| STOP2 / IWDG / 10s 浅醒 | 须看电流和唤醒 | [`low_power_stop2.md`](low_power_stop2.md) 0.2 |
| 上位机透传窗 | 同一 COM 看 NMEA | 日志区或串口助手；透传时 ulog 默认静音 |
| `rram` 业务块 API | 仅 STOP2 栈在用 | 不必为调试去写 |
| RDSS 5V PA 仅发射开 | 本版查卡/收信/透传也开 5V | 本版测完后改；见 `BSP/rdss/README.md` 2026-08-27 备注 |

---

## 3. 怎么测（入口）

USB 插着、进 CHARGE，不进 STOP2。命令一行一条 JSON，`\n` 结尾。勾选表见 [`board_bringup.md`](board_bringup.md) 第 12 节。

```text
ping → test.rtc → test.key → test.adc → test.led
→ stream.set gnss → 关 → test.gnss.fix
→ test.rdss.card / send → test.session.once
→ 拔 USB 测 MODE / 假关机 10s / STOP2
```
