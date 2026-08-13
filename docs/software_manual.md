# 整机软件说明（产品层）

> 范围：`app/`、`BSP/`、`services/`、`board/`、`config/`。  
> 不展开：`middlewares/rt-thread`、`firmware`、`DeviceDrivers`（仅作底层依赖说明）。  
> 日期：2026-08-13  
> 板内 `test.*` CLI 与上位机 V0.1 见第 8 章 / `tools/host_pc`。

---

## 1. 总体架构

### 1.1 分层

```text
┌─────────────────────────────────────────────────────────┐
│  services/cli          USB JSON 控制面（上位机入口）      │
│  services/log          ulog → CDC 异步日志               │
│  services/stream       透传通道抽象                       │
│  services/cfg          运行时配置（RAM）                  │
├─────────────────────────────────────────────────────────┤
│  app/mode              整机状态机（唯一切态权威）          │
│  app/session           报文会话调度（ALARM/ON/LOW_BATT）  │
│  app/session/msg_pack  短报文组包                         │
├─────────────────────────────────────────────────────────┤
│  BSP/key  led  adc  gnss  rdss  rtc  pwr  pm            │
│  （按键/灯/电量/定位/北斗短报文/日历/共享电源/MCU浅睡）   │
├─────────────────────────────────────────────────────────┤
│  board/                引脚、时钟恢复、USB 底层、SysTick  │
│  config/               USE_* 开关、CDC/stream 配置        │
├─────────────────────────────────────────────────────────┤
│  DeviceDrivers / firmware / RT-Thread / CherryUSB        │
└─────────────────────────────────────────────────────────┘
```

### 1.2 依赖原则

| 方向 | 规则 |
|------|------|
| KEY / ADC → MODE | 只 `mode_post_event()`，不直接改状态 |
| MODE → session / gnss / rdss / led | MODE 编排启停 |
| session → gnss / msg_pack / rdss | 一条完整「定位→组包→发信」 |
| GNSS/RDSS → pwr_plna | 共享 PLNA 引用计数 |
| pm ↔ 业务 | 用 `pm_lock/unlock` 解耦（目前业务尚未调用） |

### 1.3 启动顺序（INIT_*）

| 级 | 模块 |
|----|------|
| BOARD | 引脚/UART 等 DeviceDrivers |
| DEVICE | USB CDC `cdc_acm` |
| COMPONENT | `cfg`、`stream`、`ulog_cdc_be` |
| APP | `mode`、`key`、`led`、`adc`、`gnss`、`rdss`、`rtc`、`pm`、`cli` |
| 懒启动 | `session`（首次 `session_*` 时建线程） |

`main()` 几乎空转；业务全靠自动初始化。

---

## 2. 线程一览

| 线程名 | 模块 | 默认优先级 | 作用 |
|--------|------|------------|------|
| `tidle` | RT-Thread | 最低 | 空闲；挂 `pm` STOP0 hook |
| `timer` | RT-Thread 软定时 | 4 | session 2/5/10min |
| `mode` | app/mode | 14 | 状态机 |
| `key` | BSP/key | 15 | 按键滤波 → 事件 |
| `cli` | services/cli | 16 | JSON 命令 |
| `session` | app/session | 18 | 报文会话 |
| `adcbat` | BSP/adc | 20 | 周期采电 |
| `rtc` | BSP/rtc | 20 | 校时落盘 |
| `led` | BSP/led | 21 | 灯效 |
| `gnss` | BSP/gnss | 22 | 定位/透传 |
| `rdss` | BSP/rdss | 22 | 发信/透传 |
| `ulog_async` | ulog | 30 | 异步打日志到 CDC |

---

## 3. 核心调用链（业务）

### 3.1 告警发信

```text
KEY 长按/FALL
  → mode_post_event(SOS_LONG/FALL)
  → mode: enter_alarm → session_alarm_start()
  → session: gnss_start_fix() → 等结果
  → msg_pack_loca_up(..., alarm=1)
  → rdss_start_send() → 等 FKI
  → 定时 2min/5min 重复；满 48h → MODE_EVT_ALARM_EXPIRE → ON + session_on_start
```

### 3.2 开机 10 分钟

```text
BATT 短按确认 → ON → session_on_start()
  → 立刻一拍（alarm=0）+ 每 10min
离 ON（关机/充电/告警/透传/保护）→ session_stop_all()
```

### 3.3 低电一条

```text
adc: OK→WARN → MODE_EVT_BAT_WARN
  → 仅 ON 时 → LOW_BATT → session_lowbatt_once()
  → 发 1 条 → MODE_EVT_LOW_BATT_DONE → 回 ON + session_on_start
```

### 3.4 透传

```text
CLI stream.set → stream → mode_passthru_set
  → mode: PASSTHRU → gnss/rdss_passthru_enter
USB CDC RX ↔ 模块 UART（经 stream/passthru_write）
```

### 3.5 浅睡

```text
tidle → pm_idle_hook →（lock==0）STOP0
  → 醒：board_clock_resume_after_stop()
```

---

## 4. 模块与 API 说明

### 4.1 `app/mode` — 整机状态机

**功能：** 唯一模式权威；消费事件，启停会话与透传，同步 LED。

| API | 用法 | 说明 |
|-----|------|------|
| `mode_init` | INIT 自动 | 建 `"mode"` 事件与线程；上电采 USB/保护 |
| `mode_state_get` | 查询 | 当前 `MODE_ST_*` |
| `mode_post_event` | KEY/ADC/session | 置位事件，线程内 `mode_dispatch` |
| `mode_passthru_set` | stream/CLI | 异步申请透传掩码；0=退出 |
| `mode_passthru_flags_get` | stream | 当前透传通道 |

**状态：** OFF / BATT / ON / ALARM / CHARGE / FORCE_OFF / PASSTHRU / LOW_BATT；（SLEEP 预留空）

**事件位：** `SOS_SHORT/LONG`、`FALL`、`USB_IN/OUT`、`BATT_TO`、`BAT_PROTECT`、`BAT_WARN`、`PT_APPLY`、`ALARM_EXPIRE`、`LOW_BATT_DONE`

**下层调用：** `session_*`、`gnss/rdss_passthru_*`、`gnss/rdss_on_bat_protect`、`led_post_mode`、`adc_bat_get_level`、USB 脚读电平

**细则：** `app/mode/fsm.md`

---

### 4.2 `app/session` — 会话调度

| API | 功能 |
|-----|------|
| `session_alarm_start/stop` | ALARM：立刻 + 2/5min，48h 通知 MODE |
| `session_on_start/stop` | ON：立刻 + 10min，报文模式 `N` |
| `session_lowbatt_once` | 只发 1 条，完成后 `LOW_BATT_DONE` |
| `session_stop_all` | 停当前任意会话并关射频 |

**一拍流程（`run_one_cycle`）：**

1. `sim_present()` 无卡则跳过  
2. `gnss_start_fix` + 等 `gnss_result_mq`  
3. 可选 `rdss_ensure_card`  
4. `msg_pack_loca_up`  
5. `rdss_start_send` + 等 `rdss_result_mq`

**重要：** 需 `RT_USING_TIMER_SOFT`（已开）。时间轴目前用 `rt_tick`，非 RTC Unix。

---

### 4.3 `app/session/msg_pack`

| API | 功能 |
|-----|------|
| `msg_pack_loca_up(out, max, fix, alarm_mode)` | 组 MBA01 风格包；`alarm_mode≠0`→`'A'`，否则 `'N'` |

**依赖：** `cfg` 设备 ID、`adc_bat` 电量、`gnss_fix_t` 坐标。

---

### 4.4 `BSP/key`

| API | 功能 |
|-----|------|
| `key_init` | EXTI + `"key"` 线程 |
| `sim_present` | SIM 在位（低有效，已滤波） |
| `board_key_irq_init` | 注册 EXTI 回调（内部） |

**上报 MODE：** 短按/长按 SOS、FALL、USB_IN/OUT（双沿滤波）

---

### 4.5 `BSP/adc`

| API | 功能 |
|-----|------|
| `adc_bat_init` / `adc_bat_read` | 初始化 / 读采样结构 |
| `adc_bat_get_mv/percent/level` | 电压、百分比、OK/WARN/PROTECT |

**边沿事件：** 进 PROTECT→`BAT_PROTECT`；进 WARN→`BAT_WARN`（MODE 仅 ON 受理）

---

### 4.6 `BSP/led`

| API | 功能 |
|-----|------|
| `led_init` | `"led"` 线程 |
| `led_post_percent` | ADC 推电量动画 |
| `led_post_mode` | MODE 推视图（PASSTHRU+USB 显示充电外观） |

---

### 4.7 `BSP/gnss`

| API | 功能 |
|-----|------|
| `gnss_init` | 线程/电源脚 |
| `gnss_start_fix` | 非阻塞一次定位 |
| `gnss_result_mq` | 结果队列 |
| `gnss_get_fix` | 读最近有效点 |
| `gnss_passthru_*` | 透传进出/写 |
| `gnss_on_bat_protect` | 强制关电 |

**电源：** `pwr_plna` + `EN_PGNSS` / `EN_LNA_GNSS`（模块自管）

---

### 4.8 `BSP/rdss`

| API | 功能 |
|-----|------|
| `rdss_init` | 线程/电源脚 |
| `rdss_start_send` | 上电→波束→PA→CCTCQ→FKI→关电 |
| `rdss_ensure_card` / `rdss_get_card_id` | `$CCICR`/`$BDICP` 卡号 |
| `rdss_passthru_*` / `rdss_on_bat_protect` | 透传 / 关电 |
| `rdss_result_mq` | 结果队列 |

**配置：** 收信号 `cfg_get_recv_id()`；PA 受 `pa_enable`；卡号同步 `device_id`

---

### 4.9 `BSP/pwr`

| API | 功能 |
|-----|------|
| `pwr_plna_init/acquire/release/refcount` | 仅共享 `MCU_EN_PLNA` |

GNSS/RDSS **专用** EN 脚不放这里。

---

### 4.10 `BSP/rtc`

| API | 功能 |
|-----|------|
| `rtc_hw_init` | LSE + 设备 `"rtc"` + 校时线程 |
| `rtc_get_unix` | 读 UTC（直接读寄存器） |
| `rtc_post_unix` | 非阻塞投递校时（唯一写入口） |
| `rtc_is_synced` | 是否校过 |

**缺口：** GNSS/RDSS **尚未**调用 `rtc_post_unix`。

---

### 4.11 `BSP/pm`

| API | 功能 |
|-----|------|
| `pm_idle_hook_install` | 注册 idle→STOP0（INIT 已调） |
| `pm_lock` / `pm_unlock` / `pm_lock_count` | 嵌套禁止浅睡 |

醒后：`board_clock_resume_after_stop()`。假关机 STOP2：**不做**（文档预留）。

---

### 4.12 `services/cfg`

| API | 功能 |
|-----|------|
| `cfg_get/set_recv_id` | 收信卡号 |
| `cfg_get/set_pa_enable` | 是否开 PA |
| `cfg_get/set_device_id` | 报文设备 ID |
| `cfg_to_json` | CLI 导出 |

默认 RAM，Flash 固化后补。

---

### 4.13 `services/stream` / `cli` / `usb` / `log`

| 模块 | 要点 |
|------|------|
| stream | `stream_set_enable` 联动 `mode_passthru_set`；`stream_write` 下行透传 |
| cli | 一行 JSON；命令见 `services/cli/README.md` |
| cdc | `cdc_acm_write` / RX 回调；DTR 控制日志是否真发出 |
| ulog_cdc | 异步缓冲 `ULOG_ASYNC_OUTPUT_BUF_SIZE`（现 16KB）；满则**丢新留旧** |

---

### 4.14 `board`

| API/文件 | 功能 |
|----------|------|
| `board_pins.h` | 全板 IO 宏 |
| `rt_hw_init` | SysTick、堆、板级 INIT |
| `board_clock_resume_after_stop` | STOP 后 HSE+PLL+USB clk |
| `usb_hw` / `usb_dc_low_level_init` | USB 时钟中断上拉 |

堆：`0x20012000`～`0x20024000`（72KB，含 R-SRAM 高端）。

---

## 5. 配置开关（`product_config.h`）

| 宏 | 模块 |
|----|------|
| `USE_USB_CDC` | USB/日志 CDC |
| `USE_CLI` | JSON CLI |
| `USE_KEY` / `USE_LED` / `USE_ADC_BAT` | 按键/灯/电量 |
| `USE_GNSS` / `USE_RDSS` | 定位/短报文 |
| `USE_RTC` / `USE_PM` | 日历 / STOP0 |

---

## 6. 内存与资源（量级）

| 项 | 约值 |
|----|------|
| Flash（text+data） | ~127 KB / 512 KB |
| 静态 RAM（data+bss） | ~23 KB |
| 堆区上限 | 72 KB |
| 片内 SRAM 总量 | 144 KB（**未超**） |

---

## 7. 已知缺口（完整检查摘要）

| 项 | 状态 |
|----|------|
| MODE + ALARM 2/5min + ON 10min + LOW_BATT | 已实现 |
| IDLE STOP0 | 已实现；业务未 `pm_lock` |
| 假关机 SLEEP/STOP2 | **预留空，不做** |
| GNSS/RDSS → `rtc_post_unix` | **未接** |
| cfg Flash 持久化 | **未做** |
| 首次定位时间等 stub | 组包侧仍有占位 |
| ulog 满缓冲 | 丢新；已加大到 16KB |

---

## 8. 测试函数与上位机

### 8.1 板内测试函数（已实现）

实现：`services/cli/cli_test.c`，经 USB JSON CLI 触发。详例见 `services/cli/README.md`。

| 块 | 测试项 | 命令 |
|----|--------|------|
| mode | 读当前态 | `{"cmd":"mode.get"}` |
| key | 读电平 + sim_present | `{"cmd":"test.key"}` |
| adc | 读 mV/%/level | `{"cmd":"test.adc"}` |
| led | 强制 mode/percent | `{"cmd":"test.led","mode":2,"pct":50}` |
| gnss | 一次定位并回 JSON | `{"cmd":"test.gnss.fix"}` |
| rdss | 查卡 / 发测试载荷 | `{"cmd":"test.rdss.card"}` / `test.rdss.send` |
| session | 强制一拍（不切 MODE） | `{"cmd":"test.session.once"}` |
| rtc | get/set unix | `{"cmd":"test.rtc"}` / 带 `"unix"` |
| pm | lock 计数 / hold | `{"cmd":"test.pm"}` |

原则：测试命令 **不绕过安全门限**（保护态禁发信等仍由 MODE / 会话约束）。`test.session.once` 会打断当前会话周期。

### 8.2 上位机调试软件

| 项 | 状态 |
|----|------|
| 形态 | **已做 V0.1**：`tools/host_pc`（Python + PySide6） |
| 连接 | USB CDC；打开串口时拉 DTR |
| 双通道 | `type==rsp` → 应答，其余 → 日志区 |
| 功能页 | 监视 / 配置 / 自检 / 透传开关 / IO / 原始命令 + 日志保存 |
| 协议 | 复用 JSON CLI（含 `test.*`） |
| 用法 | 见 [`tools/host_pc/README.md`](../tools/host_pc/README.md) |

### 8.3 后续

1. 透传终端窗、电量曲线  
2. 一键自检报告导出

---

## 9. 文档索引

| 文档 | 内容 |
|------|------|
| 本文 | 软件总说明 |
| `app/mode/fsm.md` | 状态机定稿 |
| `app/session/README.md` | 会话节奏 |
| `BSP/*/README.md` | 各驱动 |
| `services/cli/README.md` | JSON 命令 |
| `docs/low_power_stop2.md` | 深睡预留 |
| `docs/roadmap_power_msg.md` | 电源/报文路线 |

---

## 10. 源码注释约定（后续改代码时）

- 公共 API：在 `.h` 用中文说明「谁调用、何时用、返回值」。  
- `.c` 内：只对 **状态转移、电源时序、协议关键分支** 加简短中文注释，避免废话。  
- 中间件 / 原厂库：**不改**注释风格。
