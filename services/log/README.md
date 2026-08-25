# services/log

ulog 自定义 backend：异步日志输出到 USB CDC。

流程图：[`flow.md`](flow.md)。

> **变更记录（2026-08-19）**  
> 透传占用同一 CDC 时默认静音本后端，避免 ulog 插进 NMEA/`$BD` 行中间。策略跟 MODE `PASSTHRU`，不写 `cfg` Flash。

- 实现：`ulog_cdc_be.c` / `ulog_cdc_be.h`
- 依赖：`services/usb`（`cdc_io.h`）、`ulog` 组件
- 注册：`INIT_COMPONENT_EXPORT(app_ulog_cdc_be_init)`
- 版本横幅：不走 USART1。`rt_hw_init` 里无 console；主机打开 COM（DTR=1）后，ulog 异步线程在首行日志前打一次 RT-Thread 版本。DTR 回调只置位，不在 USB 中断里启定时器。PA9 是 GNSS LNA，`RT_USING_USART1` 保持关闭。

## 透传静音

ulog 与 GNSS/RDSS 透传共用一条 USB CDC。异步线程随时 `cdc_acm_write`，会把一行 NMEA 从中间截断。

| 做法 | 为何用 / 不用 |
|------|----------------|
| **MODE 进出 PASSTHRU 自动 hold**（默认） | 所有进/出路径一致；`stream.set` 是异步申请，真正开关在 MODE 线程 |
| **`cfg` Flash** | 不用。`cfg` 是身份/偏移等产品项；透传是调试态，不应掉电记住「当时在静音」 |
| **`log.lvl` 降到 0** | 不用当开关。改的是全局过滤，退出透传容易忘恢复 |
| **`log.cdc` `passthru_mute`** | 仅 RAM 覆盖：要在透传里看固件日志时临时 `passthru_mute:0` |

默认 `ULOG_CDC_PASSTHRU_MUTE_DEFAULT=1`。静音只挡 **ulog CDC 后端**；JSON CLI 应答、模块透传原文仍走 `cdc_acm_write`。

```json
{"id":8,"cmd":"log.cdc"}
{"id":8,"cmd":"log.cdc","passthru_mute":0}
{"id":8,"cmd":"log.cdc","passthru_mute":1}
```

应答：`passthru_mute`（策略）、`held`（当前是否在 PASSTHRU 占用）。掉电或复位后策略回到默认 1。
