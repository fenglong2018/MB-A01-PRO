# CLI（USB JSON 控制面）

通过 **USB 虚拟串口** 发送一行 JSON，设备回一行 `{"type":"rsp",...}`。  
日志仍走 ulog（无 `type` 字段），可用串口工具同时看 LOG 与应答。

收包流程图：[`flow.md`](flow.md)。模块总表：[`docs/module_status.md`](../../docs/module_status.md)。

> **变更记录（2026-08-19）**  
> 增加 `log.cdc`（透传静音 CDC ulog，仅 RAM）。`log.lvl` / `cfg.*` 行为不变。

## 开关

`config/product_config.h`：`USE_CLI 1`（依赖 `USE_USB_CDC 1`）

## 参数宏

| 宏 | 头文件 | 含义 |
|----|--------|------|
| `CLI_RX_RB_SIZE` 等 | `cli.h` | 收包缓冲 / 线程 |
| `CLI_JSON_*` | `cli_json.h` | 解析临时缓冲 |
| `LOG_TAG` / `LOG_LVL` | `cli.h` | 本模块 ulog |

## 使用前

1. `make flash` 后插 USB，打开对应 COM  
2. 串口工具打开口（置 DTR），再发命令  
3. 命令以 `\n` 或 `\r\n` 结尾，**一行一条**

## 命令示例

```json
{"id":1,"cmd":"ping"}
{"id":2,"cmd":"stream.list"}
{"id":3,"cmd":"stream.set","name":"gnss","enable":1}
{"id":4,"cmd":"stream.get","name":"rdss"}
{"id":5,"cmd":"io.list"}
{"id":6,"cmd":"io.get","pin":"EN_5V"}
{"id":7,"cmd":"io.set","pin":"EN_5V","val":1}
{"id":8,"cmd":"log.lvl","lvl":6}
{"id":9,"cmd":"log.lvl","lvl":3,"tag":"cli"}
{"id":27,"cmd":"log.cdc"}
{"id":28,"cmd":"log.cdc","passthru_mute":0}
{"id":10,"cmd":"cfg.get"}
{"id":11,"cmd":"cfg.set","recv_id":13500001,"pa_enable":0,"device_id":1325000001,"charge_offset_mv":100}
{"id":12,"cmd":"test.list"}
{"id":13,"cmd":"mode.get"}
{"id":14,"cmd":"test.key"}
{"id":15,"cmd":"test.adc"}
{"id":16,"cmd":"test.led","mode":2,"pct":50}
{"id":17,"cmd":"test.gnss.fix"}
{"id":18,"cmd":"test.rdss.card"}
{"id":19,"cmd":"test.rdss.send"}
{"id":20,"cmd":"test.session.once"}
{"id":21,"cmd":"test.rtc"}
{"id":22,"cmd":"test.rtc","unix":1735689600}
{"id":23,"cmd":"test.pm"}
{"id":24,"cmd":"test.pm","lock":1}
{"id":25,"cmd":"test.pm","unlock":1}
{"id":26,"cmd":"test.pm","hold_ms":3000}
```

### 应答

- 成功：`{"type":"rsp","id":1,"ok":1,...}`  
- 失败：`{"type":"rsp","id":3,"ok":0,"err":"not_built"}`

常见 `err`：`not_built`（模块未编译）、`not_ready`（IO 未接线控）、`unknown_cmd`、`no_cmd`、`timeout`、`busy_passthru`

### mode.get / test.*

板内自检（实现于 `cli_test.c`）。`test.gnss.fix` / `test.rdss.*` 会在 CLI 线程内阻塞等待，期间勿连发命令。

| 命令 | 说明 |
|------|------|
| `mode.get` | 当前 MODE 态（含 `FAKE_OFF`）+ 透传 flags |
| `test.key` | SOS/USB/FALL/SIM 引脚电平 + `sim_present` |
| `test.adc` | 强制采一拍；mV / lookup / % / charge / offset / level |
| `test.led` | 可选 `mode`、`pct` → `led_post_*` |
| `test.gnss.fix` | 一次定位；可选 `wait_ms`；应答含 `unix`（RMC；无日期为 0）。**不写 RTC** |
| `test.rdss.card` | 查卡号；可选 `wait_ms` |
| `test.rdss.send` | 发载荷 `TEST`；可选 `wait_ms` |
| `test.session.once` | 强制一拍（**不改 MODE**）；会打断当前会话 |
| `test.rtc` | 读 unix（未校时为 0）+ `synced`；带 `"unix":N` 则校准 |
| `test.pm` | 读 `lock_count`；`lock`/`unlock`/`hold_ms` |
| `test.list` | 列出上述命令 |

### cfg.*

运行时配置（身份已落 Flash；`pa_enable` 仅 RAM）：

| 字段 | 默认 | 含义 |
|------|------|------|
| `recv_id` | 13500001 | 短报文收信卡号 |
| `pa_enable` | 0 | 有效波束后是否开 PA |
| `device_id` | 1325000001 | 设备编号（Flash）；RDSS `$BDICP` 不覆盖 |
| `charge_offset_mv` | 100 | 充电查表压差（mV），0=关；上限 500 |
| `hw_ver` | 空 | 硬件版本；`cfg.set` 可写出厂值 |
| `sw_ver` | `v0.2` | 固件宏 `CFG_SW_VER`，只读 |
| `upgrade_unix` | 0 | OTA 预留，只读 |
| `bd_card` | 0 | 当前北斗卡（号变才写 Flash） |
| `first_fix_unix` | 0 | 首次有效定位 Unix（GNSS RMC） |

JSON **只增字段、不改旧字段**。`cfg.set` 可不带 `charge_offset_mv`（板端保持原值）。`tools/host_pc` V0.2 配置页可改偏移。

短报文 MBA01 电量仍为 2 位 `00`～`99`，与 CLI 变更无关。

### log.lvl

- `lvl`：7=DBG，6=INFO，4=WARNING，3=ERROR  
- 可选 `tag`：只调某一模块级别  

不要用 `log.lvl` 充当透传静音：退出后容易忘恢复。

### log.cdc

透传与 ulog 共用一条 CDC。进入 `PASSTHRU` 时 MODE 自动 hold，默认丢弃 CDC ulog。

| 字段 | 含义 |
|------|------|
| `passthru_mute` | 可选写入；1=透传时静音（默认），0=透传仍打 ulog。**只改 RAM，不进 `cfg`** |
| `held` | 应答：当前是否在透传占用 |

见 [`../log/README.md`](../log/README.md)。

### io.*

白名单见 `cli_io.c`。骨架阶段多为 `ready:0`，`io.set`/`io.get` 返回 `not_ready`。  
接上 GPIO 后把对应项 `ready` 置 1 并实现读写即可。上板时电源轨用会话触发后万用表量，见 [`docs/board_bringup.md`](../../docs/board_bringup.md)。

### stream.*

见 [`../stream/README.md`](../stream/README.md)。`USE_GNSS`/`USE_RDSS` 为 1 时 `built:1`。

## 文件

| 文件 | 作用 |
|------|------|
| `cli.c` / `cli.h` | CDC 拼行、处理线程 |
| `cli_json.c` / `cli_json.h` | 命令分发 |
| `cli_test.c` / `cli_test.h` | `mode.get` / `test.*` 自检 |
| `cli_io.c` / `cli_io.h` | IO 白名单 |
