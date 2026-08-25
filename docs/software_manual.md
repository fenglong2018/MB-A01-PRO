# 整机软件说明（产品层）

> 范围：`app/`、`BSP/`、`services/`、`board/`、`config/`。  
> 不展开：`middlewares/rt-thread`、`firmware`、`DeviceDrivers`（仅作底层依赖说明）。  
> 日期：2026-08-19  
> **本次变更：** 透传时默认静音 CDC ulog（MODE hold；`log.cdc` RAM，不进 cfg）。
> 板内 `test.*` CLI 与上位机 V0.2 见第 8 章 / `tools/host_pc`。

---

## 1. 总体架构

### 1.1 分层

```text
┌─────────────────────────────────────────────────────────┐
│  services/cli          USB JSON 控制面（上位机入口）      │
│  services/log          ulog → CDC 异步日志               │
│  services/stream       透传通道抽象                       │
│  services/cfg          身份/配置（Flash 双槽）            │
├─────────────────────────────────────────────────────────┤
│  app/mode              整机状态机（唯一切态权威）          │
│  app/session           报文会话调度（ALARM/ON/LOW_BATT）  │
│  app/session/msg_pack  短报文组包                         │
├─────────────────────────────────────────────────────────┤
│  BSP/key led adc gnss rdss rtc pwr pm nvflash bkp_user rram │
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
| session → rtc | 仅 ON 首定位 / 未校时 ALARM 时 `rtc_post_unix` |
| GNSS/RDSS → pwr_plna | 共享 PLNA 引用计数 |
| cfg → nvflash | 仅身份块；mode 不 include nvflash |
| mode → bkp_user | 仅粘性块；cfg 不 include bkp_user |
| pm ↔ 业务 | `pm_lock`：nvflash 擦写时占用；idle hook 不擦 Flash |

### 1.3 启动顺序（INIT_*）

| 级 | 模块 |
|----|------|
| BOARD | 引脚/UART 等 DeviceDrivers |
| DEVICE | USB CDC `cdc_acm` |
| COMPONENT | `stream`、`ulog_cdc_be` |
| ENV | `cfg` LOAD Flash、`adcbat`、`rram`、`rtc`（LSE+校时线程） |
| APP | `cfgnv` 线程、`mode`（读 BKP/墙钟）、`key`、`led`、`gnss`、`rdss`、`pm`、`cli` |
| 懒启动 | `session`（首次 `session_*` 时建线程） |

`main()` 几乎空转；业务全靠自动初始化。

### 1.4 三套库与复位

细则：[`BSP/nvflash/README.md`](../BSP/nvflash/README.md)。

| 库 | 内容 | 丢失条件 |
|----|------|----------|
| Flash 双槽 | 身份、recv_id、设备编号、卡槽、offset、首次定位 | 整槽 CRC 失败则不用 |
| BKP（DAT3+） | ALARM/OFF/FORCE_OFF/FAKE_OFF←ALARM + `alarm_start_unix` + `lb_sent` | 真掉电；看门狗/STOP2 复位仍在 |
| R-SRAM 16KB | STOP2 入口栈（`.rram`）；业务块仍可选 | 看门狗/POR |

上电：Flash LOAD 身份 → MODE 读 BKP。保护且无 USB → FORCE_OFF（告警作废）。BKP 告警有效 → 续 ALARM（含满 72h、从 FAKE_OFF←ALARM）。否则看电量：保护则 FORCE_OFF；OFF 且 WARN 且 `lb_sent=0` 则发 1 条 N。**不恢复 ON**。

真掉电（BKP 也没了）：身份仍从 Flash LOAD；MODE 按上电默认 + 电量，**不续告警**。

### 1.5 模块完成度与流程图

各模块是否写完、CLI 用法、流程图入口：[`docs/module_status.md`](module_status.md)。

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
| `adcbat` | BSP/adc | 20 | 通知采电（上电 1 次；BATT/CHARGE/FAKE_OFF 10s/会话前） |
| `cfgnv` | services/cfg | 22 | 身份 Flash SAVE |
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
  → 定时 2min / 5min / 10min 重复；满 72h 仍停 ALARM（报文 A，灯双闪）
  → ON 走独立 session_on_start（立刻 1 条 N + 每 10min）
  → 每拍 SHOT_BUSY / SHOT_IDLE：MODE 进出 FAKE_OFF
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
  → ON / FAKE_OFF←ON / OFF / 关机看电 → LOW_BATT → session_lowbatt_once()
  → 发 1 条 N → MODE_EVT_LOW_BATT_DONE → 回进入前的态
  → BKP lb_sent=1；STOP2 复位后仍 WARN 不再发
  → 回到 OK → MODE_EVT_BAT_OK 清 lb_sent
告警不进 LOW_BATT
```

### 3.4 透传

```text
CLI stream.set → stream → mode_passthru_set
  → mode: PASSTHRU（先静音 CDC ulog）→ gnss/rdss_passthru_enter
模块 UART → stream_write → USB CDC；PC 非 JSON 行 → passthru_write
退出 / 通道全失败 / 保护离开 PASSTHRU → 恢复 CDC ulog
不查 SIM：无卡也可上电调试
需要透传时仍看 ulog：log.cdc passthru_mute:0（RAM，不进 cfg）
```

### 3.5 浅睡 / 深睡

```text
tidle → pm_idle_hook →（lock==0）STOP0
FAKE_OFF → RTC 10s WakeUp：闪灯 + 空载采电 + 喂狗
OFF / FORCE_OFF → pm_stop2_enter（R-SRAM 栈，醒后复位）
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

**状态：** OFF / BATT / ON / ALARM / CHARGE / FORCE_OFF / PASSTHRU / LOW_BATT / **FAKE_OFF**

**事件位：** `SOS_SHORT/LONG`、`FALL`、`USB_IN/OUT`、`BATT_TO`、`BAT_PROTECT`、`BAT_WARN`、`BAT_OK`、`PT_APPLY`、`SIM`、`LOW_BATT_DONE`、`SHOT_BUSY`、`SHOT_IDLE`、`RTC_WU`

**下层调用：** `session_*`、`mode_alarm_anchor_latch`（session 锁告警节奏锚点）、`gnss/rdss_passthru_*`、`gnss/rdss_on_bat_protect`、`led_post_mode`、`adc_bat_get_level`、USB 脚读电平

**细则：** `app/mode/fsm.md`

---

### 4.2 `app/session` — 会话调度

| API | 功能 |
|-----|------|
| `session_alarm_start/stop` | ALARM：立刻 + 2/5/10min（满 72h 仍 A）；拍前后 `SHOT_*` |
| `session_alarm_resume` | 按已有 Unix 锚点续告警（0=未校时，一直 2min） |
| `session_on_start/stop` | ON：立刻 + 10min；进入后第一次有效 GNSS 校 RTC |
| `session_lowbatt_once` | 只发 1 条，完成后 `LOW_BATT_DONE` |
| `session_stop_all` | 停当前任意会话并关射频 |

**一拍流程（`run_one_cycle`）：**

1. `sim_present()` 无卡则跳过（不开射频）；拍中拔卡不中断  
2. ADC（保护则跳过）  
3. `gnss_start_fix` + 等 `gnss_result_mq`（RMC→`unix_sec`；按策略 `rtc_post_unix`）  
4. 可选 `rdss_ensure_card`  
5. `msg_pack_loca_up`  
6. `rdss_start_send` + 等 `rdss_result_mq`

**重要：** 需 `RT_USING_TIMER_SOFT`（已开）。ALARM 2/5/10min 用 **已校时的 RTC Unix**。未校时锚点=0。短报文时刻用 GNSS RMC，不读 RTC。校时策略见 `BSP/rtc/README.md`。

---

### 4.3 `app/session/msg_pack`

| API | 功能 |
|-----|------|
| `msg_pack_loca_up(out, max, fix, alarm_mode)` | 组 MBA01 风格包；`alarm_mode≠0`→`'A'`，否则 `'N'` |

**依赖：** `cfg` 设备编号、首次定位 Unix（GNSS RMC 第一次非 0）、`adc_bat` 电量、`gnss_fix_t`（含 `unix_sec`）。  
电量字段：**2 位 ASCII `00`～`99`（协议未改）**；值来自发信前空载采样（CHARGE 退出后第一拍用缓存）。

---

### 4.4 `BSP/key`

| API | 功能 |
|-----|------|
| `key_init` | EXTI + `"key"` 线程 |
| `sim_present` | SIM 在位（低有效，已滤波） |
| `board_key_irq_init` | 注册 EXTI 回调（内部） |

**上报 MODE：** 短按/长按 SOS、FALL、USB_IN/OUT（双沿滤波）。SIM 更新 `sim_present()` 并投 `MODE_EVT_SIM`（假关机 ON 用来开关 10s 灯）；透传不查卡。

---

### 4.5 `BSP/adc`

| API | 功能 |
|-----|------|
| `adc_bat_init` / `adc_bat_read` | 初始化 / 读采样结构 |
| `adc_bat_request` / `sample_wait` | 通知采一拍 / 等待（CHARGE 退出后第一拍跳过） |
| `adc_bat_set_charging` / `pause` | 充电周期开停 / 透传停采 |
| `adc_bat_get_mv/percent/level` | 电压、百分比、OK/WARN/PROTECT |

**边沿事件：** 进 PROTECT→`BAT_PROTECT`；进 WARN→`BAT_WARN`（MODE：ON / FAKE_OFF←ON / OFF / 关机看电；告警忽略）；回到 OK→`BAT_OK`（清 `lb_sent`）  
充电查表减 `charge_offset_mv`（见 cfg）。细则：`BSP/adc/README.md`。

**上位机：** MBA01 电量格式不变。`tools/host_pc` V0.2 配置页可改 `charge_offset_mv`，监视显示 lookup/charge。

---

### 4.6 `BSP/led`

| API | 功能 |
|-----|------|
| `led_init` | `"led"` 线程 |
| `led_post_percent` | ADC 推电量动画 |
| `led_post_mode` | MODE 推视图（`FAKE_OFF←ALARM` 显示 ALARM；`FAKE_OFF←ON` 有卡显示 ON、无卡灭灯；PASSTHRU+USB 显示充电外观） |

ON：有卡从开机起 RTC 每 10s 亮 100ms（搜星和假关机同一条 WakeUp，不靠 SysTick 空等）。无卡开机：5s 内每秒亮 100ms，再假关机灭灯。ALARM：每 10s 双闪（100ms + 灭 200ms + 100ms）。BATT：流水各档 300ms，最后一档保持到进看电起 5s。`FAKE_OFF←ALARM` 由 RTC `LED_MSG_HB` 双闪。细则：`BSP/led/README.md`。

---

### 4.7 `BSP/gnss`

| API | 功能 |
|-----|------|
| `gnss_init` | 线程/电源脚 |
| `gnss_start_fix` | 非阻塞一次定位 |
| `gnss_result_mq` | 结果队列 |
| `gnss_get_fix` | 读最近有效点（含 `unix_sec`） |
| `gnss_passthru_*` | 透传进出/写 |
| `gnss_on_bat_protect` | 强制关电 |

**定位：** GGA quality≥1。**时间：** 同拍 RMC `status=A`+`ddmmyy` → `unix_sec`（无 RMC 则为 0）。透传不解析。GNSS **不**写 RTC。

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

**配置：** 收信号 `cfg_get_recv_id()`；PA 受 `pa_enable`（仅 RAM）；`$BDICP` 走 `cfg_note_bd_card`，**不改** `device_id`。**不校 RTC。**

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
| `rtc_get_unix` | UTC 秒；**未校时返回 0** |
| `rtc_post_unix` | 非阻塞投递（唯一写入口） |
| `rtc_is_synced` | BKP DAT2 / RAM |

`INIT_ENV`。产品：`session` 在 ON 首定位、未校时 ALARM 时 `post(RTC_SRC_GNSS)`。CLI `test.rtc` 仍可人工校。JSON **不配**日历。细则：`BSP/rtc/README.md`。

---

### 4.11 `BSP/pm`

| API | 功能 |
|-----|------|
| `pm_idle_hook_install` | 注册 idle→STOP0（INIT 已调） |
| `pm_lock` / `pm_unlock` / `pm_lock_count` | 嵌套禁止浅睡 |

醒后：`board_clock_resume_after_stop()`。MODE 非 `FAKE_OFF` 时 `pm_lock`（看电/充电/发信都不浅睡）。仅 `FAKE_OFF` 放行 STOP0+RTC 10s。USB 有线 idle 不 STOP0。nvflash/会话/KEY 滤波再叠 lock。`OFF`/`FORCE_OFF`：`pm_stop2_enter()`。IWDG：`BSP/iwdg`。

---

### 4.12 `services/cfg`

| API | 功能 |
|-----|------|
| `cfg_get/set_recv_id` | 收信卡号（Flash） |
| `cfg_get/set_pa_enable` | 是否开 PA（仅 RAM） |
| `cfg_get/set_device_id` | 设备编号（Flash；`$BDICP` 不改） |
| `cfg_note_bd_card` | 北斗卡号；变了才进 4 槽历史 |
| `cfg_get/set_charge_offset_mv` | 充电查表压差 |
| `cfg_get/set_first_fix_unix` | 首次定位 Unix（GNSS RMC，只写一次） |
| `cfg_get_hw_ver` / `cfg_set_hw_ver` | 硬件版本（出厂可写） |
| `cfg_get_sw_ver` | 软件版本（`CFG_SW_VER`） |
| `cfg_get_upgrade_unix` | OTA 预留，现 0 |
| `cfg_to_json` | CLI 导出 |

三套库：身份 → Flash（`nvflash`）；MODE/告警锚点 → BKP（`bkp_user`，mode 写）；R-SRAM 预留 STOP2（`rram`）。互不包含。细则：`BSP/nvflash/README.md`、`services/cfg/README.md`。

---

### 4.12.1 `BSP/nvflash` / `bkp_user` / `rram`

| 模块 | 要点 |
|------|------|
| nvflash | `0x0807F000` / `0x0807F800`；magic+seq+CRC；坏槽整段丢弃 |
| bkp_user | DAT3 起；不碰 RTC 的 DAT1/DAT2 |
| rram | `.rram` @ `0x20020000`；堆不占用 |

---

### 4.13 `services/stream` / `cli` / `usb` / `log`

| 模块 | 要点 |
|------|------|
| stream | `stream_set_enable` 联动 `mode_passthru_set`；`stream_write` 下行透传 |
| cli | 一行 JSON；命令见 `services/cli/README.md` |
| cdc | `cdc_acm_write` / RX 回调；DTR 控制日志是否真发出 |
| ulog_cdc | 异步缓冲 16KB，满则**丢新留旧**；`PASSTHRU` 时默认静音本后端（`log.cdc`） |

---

### 4.14 `board`

| API/文件 | 功能 |
|----------|------|
| `board_pins.h` | 全板 IO 宏 |
| `rt_hw_init` | SysTick、堆、板级 INIT |
| `board_clock_resume_after_stop` | STOP 后 HSE+PLL+USB clk |
| `usb_hw` / `usb_dc_low_level_init` | USB 时钟中断上拉 |

堆：`0x20012000`～`0x20020000`（主 SRAM；R-SRAM `0x20020000` 起 16KB 不进堆）。

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
| Flash（程序） | ~127 KB / **508 KB**（末尾 4KB 参数双槽） |
| 静态 RAM（data+bss） | ~23 KB |
| 堆区上限 | `0x20012000`～`0x20020000`（约 56 KB） |
| R-SRAM | 16 KB @ `0x20020000`（STOP2 预留） |
| 片内 SRAM 总量 | 128 KB 主 RAM + 16 KB R-SRAM |

---

## 7. 已知缺口（完整检查摘要）

| 项 | 状态 |
|----|------|
| MODE + ALARM 2/5/10min + ON 10min + LOW_BATT + FAKE_OFF | **已实现**（满 72h 仍 ALARM；OFF 也可发低电 N） |
| IDLE STOP0 | 仅 `FAKE_OFF` 放行；其余 MODE 持 `pm_lock` |
| 假关机 RTC 10s 浅醒 + 空载采电 | **已接**（2026-08-18） |
| 真关机 STOP2 + IWDG | **已接代码**（2026-08-18）；电流待测 |
| GNSS → RTC | **已接**：ON 首定位；ALARM 仅未校时。RDSS 不校 |
| cfg / MODE 持久化 | 身份 Flash；MODE/告警 BKP |
| 首次定位 Unix | GNSS RMC 第一次非 0 写入 Flash；组包用 `fix.unix_sec` |
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
| 形态 | **V0.2**：`tools/host_pc`（Python + PySide6） |
| 连接 | USB CDC；打开串口时拉 DTR |
| 双通道 | `type==rsp` → 应答，其余 → 日志区（透传原文也在此） |
| 功能页 | 监视 / 配置 / 自检 / **射频（透传+GSV/PWI）** / IO / 原始命令 |
| 协议 | 复用 JSON CLI（含 `log.cdc` / `test.*`） |
| 用法 | 见 [`tools/host_pc/README.md`](../tools/host_pc/README.md) |

### 8.3 按模块上板

步骤、勾选表、透传静音与 `io.*` 替代测法：[`docs/board_bringup.md`](board_bringup.md)。

### 8.4 后续

1. CLI `io.*` 接到真实 GPIO（或明确只用会话测轨）  
2. 上位机透传终端窗、电量曲线、一键自检报告导出

---

## 9. 文档索引

| 文档 | 内容 |
|------|------|
| 本文 | 软件总说明 |
| `app/mode/fsm.md` | 状态机定稿 |
| `app/session/README.md` | 会话节奏 |
| `BSP/*/README.md` | 各驱动 |
| `services/cli/README.md` | JSON 命令 |
| `BSP/nvflash/README.md` | 三套库总览（Flash / BKP / R-SRAM） |
| `BSP/rtc/README.md` | 墙钟与 GNSS 校时策略 |
| `BSP/gnss/README.md` | 定位与 RMC 时间 |
| `docs/low_power_stop2.md` | STOP2 / 10s / IWDG（2026-08-18 已接代码） |
| `docs/roadmap_power_msg.md` | 电源/报文路线 |
| `docs/board_bringup.md` | 按模块上板验证 |

---

## 10. 源码注释约定（后续改代码时）

- 公共 API：在 `.h` 用中文说明「谁调用、何时用、返回值」。  
- `.c` 内：只对 **状态转移、电源时序、协议关键分支** 加简短中文注释，避免废话。  
- 中间件 / 原厂库：**不改**注释风格。
