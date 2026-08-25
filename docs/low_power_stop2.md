# 低功耗：STOP2 + 假关机（FAKE_OFF）

> **状态（2026-08-18）：** 代码已接。  
> - `FAKE_OFF←ALARM`：idle STOP0 + RTC 10s 浅醒双闪 + 空载采 ADC 1 次（不跑 GNSS）。  
> - `FAKE_OFF←ON`：idle STOP0 + RTC 10s 喂狗/采电；**有卡则 10s 闪灯**，无卡灭灯。  
> - 看电 BATT 整段 5s 不 STOP0/STOP2。ON 假关机立刻灭灯。无卡满 60min 真 OFF。  
> - `OFF` / `FORCE_OFF`：`pm_stop2_enter()`（R-SRAM 栈，醒后软件复位）。  
> - 硬件 IWDG：非真关机开启，超时≈26s；发信每 5s 喂；STOP2 前 `iwdg_stop2_quiet`。冷启动真关机先不开狗。  
> 板级电流、STOP2 唤醒脚、retention 需实机确认。手册 MCU 典型值见下文 **§2.0**（STOP0 90 µA / STOP2 6 µA / STANDBY 2.5 µA）。  
> 关联：`app/mode/fsm.md`、`BSP/nvflash/README.md`、`BSP/rtc/README.md`、`BSP/pm/README.md`、`BSP/iwdg/README.md`

---

## 0. 分层（现在 vs 规划）

| 层 | 状态 | 说明 |
|----|------|------|
| **IDLE + STOP0** | **已实施** `BSP/pm` | 全 RAM 保留；idle hook；`pm_lock` |
| **逻辑 FAKE_OFF** | **已实施** `app/mode` | 发完一拍进假关机；灯按 resume 闪；射频关 |
| **RTC 10s 浅醒** | **已接** | ISR：清标志、喂狗、LED/session/MODE 钩子；MODE 仅 FAKE_OFF 采电 |
| **STOP2 真关机** | **已接代码** | `OFF` / `FORCE_OFF`；灯灭；禁狗唤醒/复位位；只等人/USB |

浅睡细节见 `BSP/pm/README.md`。

### 0.1 已落地

| 项 | 现状 |
|----|------|
| 告警节奏锚点 | `rtc_get_unix()`；未校时=0；复位 `session_alarm_resume` |
| GNSS 校时 | ON 首定位 / 未校时 ALARM；RMC；RDSS 不校 |
| 身份 / 配置 | Flash 双槽 `nvflash`，仅 `services/cfg` 写 |
| 粘性 MODE | BKP `bkp_user`（DAT3 起），含 `FAKE_OFF←ALARM`、`lb_sent` |
| 逻辑假关机 | `MODE_ST_FAKE_OFF` + `SHOT_BUSY/IDLE` + RTC 10s |
| R-SRAM 段 | 链接脚本 `RETRAM`；STOP2 用 `.rram` 栈 |
| `pa_enable` | **不进 Flash** |
| 低电 N | OFF 冷启动 WARN 可发 1 条；`lb_sent` 防 STOP2 重复 |

### 0.2 实机待确认

0. **（2026-08-21 已修）** SOS 唤醒后只闪一下 BOOT0/LED3 又睡回去：唤醒即软件复位，EXTI 沿丢了，且 mode 线程抢在 key 之前 STOP2。见第 7 节最后一行。实测电流 85µA（OFF/STOP2）。  
1. STOP2 电流、USB/KEY 唤醒后复位是否干净。  
2. IWDG 在 STOP 中是否被 `IWDGWPEN` 清干净。  
3. 10s 浅醒闪灯时长、ADC 空载是否稳定。

---

## 0.3 IWDG 策略（2026-08-18 已接 `BSP/iwdg`）

| 态 | IWDG |
|----|------|
| `OFF` / `FORCE_OFF` | 本上电未开过则不开；STOP2 前 `iwdg_stop2_quiet` |
| `ON` / `ALARM` / `FAKE_OFF` / `CHARGE` / `PASSTHRU` / `LOW_BATT` | **开**；超时 ≈26s（>16s）；发信过程中每 5s 喂 |
| 关机看电 `BATT`（从 OFF） | **不开**（避免回 OFF 后关不掉） |
| 用途 | 只防卡死，**不当** 10s 闪灯节拍 |

不要开 `RT_USING_WDT`。

## 1. 目标与结论

| 项 | 结论 |
|----|------|
| 目标 | 短报文周期间隙降低**整机**功耗（MCU + 已关断的射频轨） |
| 休眠档 | **STOP2**（N32WB452） |
| 保留内容 | **MODE + 告警时间轴 + RTC/配置**；运行时丢弃后按状态重跑 |
| 切入方式 | 逻辑 **`FAKE_OFF` 已接**；以后假关机用 RTC 10s 浅醒，真关机用 STOP2 |
| 总评 | **可行**；醒后宜走「冷恢复 / 软件复位 + 读上下文」，勿在普通 SRAM 栈上原地接着跑 RTOS |

---

## 2. 芯片约束（N32WB452）

### 2.0 手册典型电流（后续优化对照，2026-08-25 记）

数据手册低功耗档（MCU 自身典型值，**不是**整机电流；外围漏电、上拉、射频轨关不断都会叠上去）：

| 模式 | 典型电流 | 保留 | 唤醒 | 本工程用法 |
|------|----------|------|------|------------|
| **STANDBY** | **2.5 µA** | 84 个备份寄存器、全部 IO、可选 RTC、16KB Retention SRAM；VBAT 可独立供电 | **100 µs** | **未用**。RTC 周期 WakeUp 不可用（须 Alarm）。比 STOP2 更深，上下文更少 |
| **STOP2** | **6 µA** | RTC 可跑、16KB Retention SRAM、**CPU 寄存器**、全部 IO | **40 µs** | **真关机** `OFF` / `FORCE_OFF`（醒后软件复位） |
| **STOP0** | **90 µA** | RTC 可跑、**全部 SRAM**、全部 IO | **20 µs** | **假关机** `FAKE_OFF` idle |

对照：

- 手册 STOP2 **6 µA** vs 板上真关机曾测 **约 85 µA** → 优化空间主要在 **IO/电源轨/上拉/未关外设**，不是再换一档 MCU 模式就能到 6 µA。
- 手册 STOP0 **90 µA** 是假关机地板；若假关机远高于此，先查 lock、灯、射频使能脚。
- STANDBY **2.5 µA** 留作以后真关机是否再降一档的选项（VBAT、Alarm 唤醒、无周期 WakeUp）。

### 2.1 内存

| 区域 | 地址 | 大小 | STOP2 |
|------|------|------|-------|
| 普通 SRAM | `0x20000000` ~ `0x2001FFFF` | 128KB | **丢失** |
| **R-SRAM** | **`0x20020000` ~ `0x20023FFF`** | **16KB** | **保持**（需 PWR 配置 retention） |
| 备份域 | RTC 日历、BKP 寄存器等 | 小 | 保持（LSE/RTC 可继续跑） |

现有链接脚本 `n32wb452_flash.ld`：程序 Flash **508KB**（末尾 4KB 参数双槽）；`RAM` 128KB，堆止于 `0x20020000`；**已声明** `RETRAM` + `.rram`。STOP2 入口切到 `.rram` 栈并开 PWR retention 位（`pm_stop2_enter`）。

### 2.2 STOP2 行为要点

- 主调压器关；HSE/HSI/PLL 关；多数外设寄存器丢失。
- **CPU 寄存器保持**，手册称从停止处继续；但普通 SRAM 已不可信 → **不能依赖原线程栈返回**。
- 可唤醒：EXTI（含 IO）、RTC 周期 WakeUp、RTC Alarm、PVD、NRST、IWDG 等。
- 醒后系统时钟先落在 **HSI**，需重新配 **HSE + PLL（144MHz）**。
- 勘误：STANDBY 下 RTC **不能**周期 WakeUp，须用 Alarm；本方案主推 STOP2，Alarm/WakeUp 均可，优先 **日历 Alarm（绝对 Unix）**。

### 2.3 为何不靠「全栈进 R-SRAM」

当前各线程栈合计约十余 KB，再加 `.bss`/USB/堆，**远超 16KB**。  
策略改为：**只保上下文；运行时视为可丢**。

---

## 3. 必须保持 vs 可丢失

### 3.1 必须保持（产品语义）

| 项 | 原因 | 建议载体 |
|----|------|----------|
| 墙钟 + 已校时标志 | 2/5/10min 依赖绝对时间 | 硬件 RTC + BKP DAT1/DAT2 |
| 逻辑 MODE | 醒来不能当成用户真关机乱开机 | **BKP DAT3+**（已实施粘性） |
| ALARM 锚点 Unix | 判断 0～24h / 24～48h / 48h 起 10min | **BKP**（已实施） |
| 下次唤醒时刻 | 与 RTC Alarm 一致（冗余校验） | RTC 硬件 + BKP（Alarm API **未做**） |
| 身份（recv_id、device_id、卡槽、offset…） | 行为正确 | **Flash**（已实施）；RAM 副本可丢 |
| `pa_enable` | 调试用 | **仅 RAM**，不持久化 |
| FORCE_OFF / 保护意图 | 避免误进告警耗电 | 与 MODE 一同写入 BKP（已实施） |

体积目标：上下文结构 **≪ 1KB**（通常几十～一两百字节）。

### 3.2 可丢失（醒来重建）

- 全部线程栈、TCB、mq/sem 内容、heap 临时对象  
- GNSS/RDSS 电源与会话、UART 行缓冲、波束/定位结果  
- LED 动画相位、ADC 显示缓存、KEY 滤波中间态  
- USB/CLI/ulog 缓冲、PASSTHRU 运行态  
- 基于 `rt_tick` 的软定时剩余时间（改墙钟后本就不该依赖）

---

## 4. 假关机 `FAKE_OFF`（2026-08-18：STOP0 + RTC 10s 已接）

### 4.1 为何单独状态（不要并进真 `OFF`）

按键细则以 `app/mode/fsm.md` 第 4～5 块为准。

| | 真 `OFF` / `FORCE_OFF` | `FAKE_OFF`（假关机） |
|--|------------------------|----------------------|
| 含义 | 用户关机 / 低压保护 | 逻辑仍在 ALARM/ON 日程上 |
| 短按 SOS | → BATT 看电 | resume=ALARM：只看电；resume=ON：真关机 |
| 长按 / FALL | FORCE_OFF 忽略；OFF 进 ALARM | 逻辑告警：退出或保持；逻辑 ON：新 ALARM |
| 2/5/10min | 无 | **有** |
| 灯 | 全灭 | ON 10s/100ms；ALARM 10s 双闪 |
| 现状 | 逻辑关机已接 STOP2 | 逻辑已接；STOP0 + RTC 10s |

### 4.2 形态（已按此实现）

- `MODE_ST_FAKE_OFF`，`s_fake_resume ∈ {ALARM, ON}`，BKP `resume` 字段。  
- CHARGE / BATT / PASSTHRU / USB 在位：**禁止**假关机。

### 4.3 谁进 STOP2 / 谁 10s 浅醒

| 逻辑态 | 2026-08-18 |
|--------|------|
| `OFF` / `FORCE_OFF` | **真关机 STOP2**（`iwdg_stop2_quiet`，灯灭） |
| `FAKE_OFF` | RTC 10s 浅醒：ALARM 双闪 + 空载采电；有卡 ON 10s 单闪；无卡只采电；idle 可 STOP0 |
| CHARGE / PASSTHRU | **禁止** STOP2；有 USB 时 idle 也不进 STOP0 |
| 关机看电 `BATT` | **禁止** STOP2；MODE 保持 `pm_lock`，不进 STOP0 |

### 4.4 推荐时序（浅醒落地后）

```text
ALARM 或 ON（醒着发信）
  → 跑一拍 GNSS/RDSS（成功/失败/无卡都算结束）
  → ON：立刻灭灯 → FAKE_OFF；无卡满 60min → 真 OFF
  → ALARM：立刻 FAKE_OFF
  → RTC 10s：浅醒喂狗 + 空载采电；仅 ALARM 双闪
  → 2/5/10min 到点：满醒 GNSS/RDSS
  → 满 72h：仍停 ALARM（A，双闪）

按键 / USB：EXTI 满醒 → 按 fsm 派发
```

真关机 STOP2（另线）：

```text
OFF / FORCE_OFF → 关 IWDG → STOP2
唤醒：人/USB EXTI → 冷恢复
```

### 4.5 示意上下文（10s 浅醒 / STOP2 实施时再定）

```c
struct pm_ctx {
    uint32_t magic;
    uint8_t  ver;
    uint8_t  mode;           /* FAKE_OFF */
    uint8_t  resume_mode;    /* ALARM / ON */
    uint8_t  flags;
    uint32_t alarm_anchor;
    uint32_t next_wake;
};
```

---

## 5. 醒后恢复策略（关键）

**推荐：STOP2 唤醒后尽快软件复位（或等价 bootstrap），上电路径读 BKP/`pm_ctx` 恢复逻辑态。**

原因：普通 SRAM（含栈）已失效，RT-Thread 无法安全从 WFI 返回。

固定顺序（规划）：

1. 识别唤醒原因（RTC / EXTI / 复位标志 / `pm_ctx.magic`）  
2. 恢复 HSE + PLL  
3. 读上下文；非法 → 按真 OFF 冷启动  
4. 按现有 `INIT_*` 重建驱动与线程  
5. 恢复为 `resume_mode`（或 `FAKE_OFF` 后立刻切回）  
6. RTC 到点 → 直接 session 一拍；按键/USB → `mode_post_event`

---

## 6. 与现有模块的分工（实施时）

| 模块 | 规划职责 |
|------|----------|
| `BSP/rram` | STOP2 可选备份块（现无业务） |
| `BSP/bkp_user` | **已承担** MODE + 告警锚点 |
| `BSP/nvflash` / `cfg` | **已承担** 身份 |
| `BSP/pm` | 现只管 STOP0；STOP2 另接，勿塞进 idle hook |
| `BSP/rtc` | 日历已有；增 **Alarm/WakeUp** API；校时仍 `rtc_post_unix` |
| `app/session` | 锚点 Unix + SHOT 事件 **已做**；WU 钩子用 Unix 判断到点 |
| `app/mode` | `FAKE_OFF` + STOP2 + `lb_sent` **已做** |
| `BSP/pwr` | 继续只管板级射频轨；进深睡前引用计数须为 0 |
| KEY EXTI | STOP2 下作为唤醒源；恢复后重新 init/滤波 |

**MODE 仍只 start/stop 会话与切态；周期与闹钟细节不塞进 mode.c。**

---

## 7. 风险与对策

| 风险 | 对策 |
|------|------|
| WFI 返回时栈已坏 | 醒后复位 / R-SRAM 极小 stub，不原地续跑 OS |
| 用 tick 做 24h/72h | 改为 `alarm_anchor` Unix（已做） |
| 休眠中按键无效 | EXTI 配为 STOP2 唤醒；恢复后重装 KEY |
| **唤醒那次按下丢掉** | 唤醒即软件复位，EXTI 的沿没了。复位最早期锁存 SOS/FALL 电平（`board_boot_*`），`key_init` 接管滤波；MODE 用 `MODE_BOOT_HOLD_MS` + `key_is_busy()` 推迟下一次 STOP2 |
| 休眠中插 USB | 非告警：醒后 CHARGE；ALARM：态不变，可醒后处理再睡 |
| 透传时误睡 | PASSTHRU **禁止** FAKE_OFF / STOP2 |
| 发失败空等 | 本拍结束也睡；可选缩短下次间隔 |
| LSE 不稳 | 旁脚勿乱翻；不关 LSE/RTC |

---

## 8. 建议实施顺序

| 步 | 内容 | 状态 |
|----|------|------|
| 1 | BKP 粘性 + session 锚点 Unix + 72h 三档 | **已完成** |
| 2 | Flash 身份双槽 + R-SRAM 链接段 | **已完成** |
| 3 | 逻辑 `FAKE_OFF` + 告警短按看电 | **已完成** |
| 4 | RTC 10s 浅醒闪灯 + FAKE_OFF 空载采电 | **已接**（2026-08-18） |
| 5 | 发信路径喂狗后打开 IWDG（超时≈26s；OFF/FORCE_OFF 不开） | **已接**（2026-08-18） |
| 6 | OFF/FORCE_OFF → STOP2 + 醒后软件复位 | **已接代码**（2026-08-18）；电流待测 |

---

## 9. R-SRAM / 栈压缩（附）

- 压缩各线程栈、RDSS/GNSS 大数组改 `static`：**值得做**，改善平时 RAM。  
- **不能**替代「只保上下文」策略；16KB 仍装不下整 OS。  
- STOP2 若用 R-SRAM：主要用于可选备份 `pm_ctx` 或极小 resume stub，而非全部线程栈。

---

## 10. 文档索引

| 文档 | 角色 |
|------|------|
| 本文 `docs/low_power_stop2.md` | STOP2 / 10s 浅醒 / IWDG 方案 |
| `BSP/nvflash/README.md` | 三套库与复位 |
| `app/mode/fsm.md` | 现行定稿（含 FAKE_OFF 功能块图） |
| `app/session/README.md` | 调度（Unix 节奏） |
| `BSP/rtc/README.md` | Alarm / 墙钟 |
| `BSP/pwr/README.md` | 板级电源轨（与 MCU STOP2 分工） |
| `docs/roadmap_power_msg.md` | 总 roadmap 入口 |
