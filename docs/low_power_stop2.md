# 低功耗：STOP2 + 假关机（SLEEP）— 方案记录

> **状态：仅文档，未实施。**  
> 日期：2026-08-12  
> 关联：`app/mode/fsm.md`、`app/session/README.md`、`BSP/rtc/README.md`、`BSP/pwr/README.md`

---

## 0. 与 IDLE STOP0 的关系（分层）

| 层 | 状态 | 说明 |
|----|------|------|
| **IDLE + STOP0** | **已实施** `BSP/pm` | 全 RAM 保留；idle hook；`pm_lock` 解耦业务 |
| **SLEEP + STOP2** | 本文其余章节，未实施 | 发完假关机；只保上下文 |

浅睡细节见 `BSP/pm/README.md`。下文专讲深睡方案。

---

## 1. 目标与结论

| 项 | 结论 |
|----|------|
| 目标 | 短报文周期间隙降低**整机**功耗（MCU + 已关断的射频轨） |
| 休眠档 | **STOP2**（N32WB452） |
| 保留内容 | **MODE + 告警时间轴 + RTC/配置**；运行时丢弃后按状态重跑 |
| 切入方式 | 新增 **`SLEEP`（假关机）**：2/5/10min 发完（或本拍结束）后进入 → STOP2；到点/按键/USB 唤醒后回到上一逻辑态 |
| 总评 | **可行**；醒后宜走「冷恢复 / 软件复位 + 读上下文」，勿在普通 SRAM 栈上原地接着跑 RTOS |

---

## 2. 芯片约束（N32WB452）

### 2.1 内存

| 区域 | 地址 | 大小 | STOP2 |
|------|------|------|-------|
| 普通 SRAM | `0x20000000` ~ `0x2001FFFF` | 128KB | **丢失** |
| **R-SRAM** | **`0x20020000` ~ `0x20023FFF`** | **16KB** | **保持**（需 PWR 配置 retention） |
| 备份域 | RTC 日历、BKP 寄存器等 | 小 | 保持（LSE/RTC 可继续跑） |

现有链接脚本 `n32wb452_flash.ld`：`RAM` 仅 128KB，`_estack = 0x20020000`；**尚未单独声明 RSRAM 段**。

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
| 墙钟 + 已校时标志 | 2/5/10min、48h 依赖绝对时间 | 硬件 RTC + BKP |
| 逻辑 MODE / `resume_mode` | 醒来不能当成用户真关机重跑开机环 | BKP（主）/ R-SRAM 备份 |
| ALARM 锚点 Unix | 判断 0～24h / 24～48h / 满 48h→ON | BKP |
| 下次唤醒时刻 | 与 RTC Alarm 一致（冗余校验） | RTC 硬件 + BKP |
| 配置（recv_id、pa_enable、device_id…） | 行为正确 | **Flash**；RAM 副本可丢 |
| FORCE_OFF / 保护意图 | 避免误进告警耗电 | 与 MODE 一同写入 |
| （可选）RDSS 卡号缓存 | 少一次 CCICR | BKP；非硬必须 |

体积目标：上下文结构 **≪ 1KB**（通常几十～一两百字节）。

### 3.2 可丢失（醒来重建）

- 全部线程栈、TCB、mq/sem 内容、heap 临时对象  
- GNSS/RDSS 电源与会话、UART 行缓冲、波束/定位结果  
- LED 动画相位、ADC 显示缓存、KEY 滤波中间态  
- USB/CLI/ulog 缓冲、PASSTHRU 运行态  
- 基于 `rt_tick` 的软定时剩余时间（改墙钟后本就不该依赖）

---

## 4. 假关机状态 `SLEEP`（规划，未改 fsm 定稿表）

### 4.1 为何单独状态（不要并进真 `OFF`）

| | 真 `OFF` | `SLEEP`（假关机） |
|--|----------|-------------------|
| 含义 | 用户关机 | 逻辑仍在 ALARM/ON 日程上，仅物理省电 |
| 短按 SOS | → BATT | 与 resume 相关：ALARM 休眠可忽略或醒后再睡；ON 休眠按开机环处理 |
| 长按 / FALL | → ALARM | 已在告警轴：唤醒后保持/继续 ALARM |
| 2/5/10min、48h | 无 | **有**（RTC） |
| 外观 | 灯灭 | 灯灭、射频关（可同 OFF） |

### 4.2 建议形态

- 新增 `MODE_ST_SLEEP`，带 `resume_mode ∈ {ALARM, ON}`。  
- 备选：MODE 仍为 ALARM/ON，另加 `pm_phase = ACTIVE|SLEEP`（少改枚举，调试时要分清逻辑态/功耗态）。  
- **推荐显式 `SLEEP`**，与真 OFF 切割清楚。

### 4.3 谁可以进 SLEEP → STOP2

| 逻辑态 | 发完/本拍结束后 |
|--------|-----------------|
| ALARM（2min/5min） | **主场景，进入 SLEEP** |
| ON（10min，实施后） | **进入 SLEEP** |
| 真 OFF / FORCE_OFF | 可选「长睡等按键/USB」（另议） |
| CHARGE / BATT / PASSTHRU | **禁止** STOP2（USB/看电/调试需活） |

### 4.4 推荐时序

```text
ALARM 或 ON（ACTIVE）
  → 跑一拍 GNSS/RDSS（成功或失败均可睡，失败也建议睡以免空烧）
  → 关 GNSS/RDSS/PLNA/PA，LED 灭
  → 用 Unix 算 next_wake（2/5/10min；48h 切 ON 用锚点）
  → 写 pm_ctx → 进入 SLEEP → STOP2

唤醒：
  RTC 到点 → 冷恢复 → 回到 resume_mode → 再跑一拍 → 再 SLEEP
  SOS / FALL / USB（EXTI）→ 冷恢复 → 按 fsm 派发事件
```

### 4.5 示意上下文（实施时再定字段）

```c
struct pm_ctx {
    uint32_t magic;
    uint8_t  ver;
    uint8_t  mode;           /* SLEEP */
    uint8_t  resume_mode;    /* ALARM / ON */
    uint8_t  flags;          /* synced 等 */
    uint32_t alarm_anchor;   /* 进入 ALARM 的 unix；非告警可为 0 */
    uint32_t next_wake;      /* 冗余 */
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
5. 恢复为 `resume_mode`（或 SLEEP 后立刻切回）  
6. RTC 到点 → 直接 session 一拍；按键/USB → `mode_post_event`

---

## 6. 与现有模块的分工（实施时）

| 模块 | 规划职责 |
|------|----------|
| 新建 `app/pm` 或 `BSP/pm` | `pm_ctx` 读写、进 STOP2、唤醒原因、醒后复位钩子 |
| `BSP/rtc` | 日历已有；增 **Alarm/WakeUp** API；校时仍 `rtc_post_unix` |
| `app/session` | 锚点改 **Unix**（去掉依赖 `rt_tick` 的 48h）；拍结束后通知可进 SLEEP |
| `app/mode` | 增 `SLEEP`；控制谁允许睡；唤醒后事件仍走 fsm |
| `BSP/pwr` | 继续只管板级射频轨；MCU STOP2 不放这里亦可交叉引用 |
| KEY EXTI | STOP2 下作为唤醒源；恢复后重新 init/滤波 |

**MODE 仍只 start/stop 会话与切态；周期与闹钟细节不塞进 mode.c。**

---

## 7. 风险与对策

| 风险 | 对策 |
|------|------|
| WFI 返回时栈已坏 | 醒后复位 / R-SRAM 极小 stub，不原地续跑 OS |
| 用 tick 做 24h/48h | 改为 `alarm_anchor` Unix |
| 休眠中按键无效 | EXTI 配为 STOP2 唤醒；恢复后重装 KEY |
| 休眠中插 USB | 非告警：醒后 CHARGE；ALARM：态不变，可醒后处理再睡 |
| 透传时误睡 | PASSTHRU **禁止** SLEEP |
| 发失败空等 | 本拍结束也睡；可选缩短下次间隔 |
| LSE 不稳 | 旁脚勿乱翻；不关 LSE/RTC |

---

## 8. 建议实施顺序（未开始）

| 步 | 内容 | 依赖 |
|----|------|------|
| 1 | `pm_ctx` + BKP 布局；session 锚点改 Unix | RTC 已有 |
| 2 | RTC Alarm API；拍结束后进 SLEEP（可先 STOP0 验证逻辑） | fsm 增 SLEEP |
| 3 | SLEEP → STOP2 + 醒后软件复位恢复 | 时钟重配 |
| 4 | EXTI 唤醒与 CHARGE/BATT/PASSTHRU 例外 | KEY |
| 5 | （可选）栈压缩、大缓冲改 static — 省 RAM，**非** STOP2 前提 | 独立 |

---

## 9. R-SRAM / 栈压缩（附）

- 压缩各线程栈、RDSS/GNSS 大数组改 `static`：**值得做**，改善平时 RAM。  
- **不能**替代「只保上下文」策略；16KB 仍装不下整 OS。  
- STOP2 若用 R-SRAM：主要用于可选备份 `pm_ctx` 或极小 resume stub，而非全部线程栈。

---

## 10. 文档索引

| 文档 | 角色 |
|------|------|
| 本文 `docs/low_power_stop2.md` | 低功耗总方案（未实施） |
| `app/mode/fsm.md` | 现行定稿 + SLEEP 规划节 |
| `app/session/README.md` | 调度与进睡衔接 |
| `BSP/rtc/README.md` | Alarm / 墙钟 |
| `BSP/pwr/README.md` | 板级电源轨（与 MCU STOP2 分工） |
| `docs/roadmap_power_msg.md` | 总 roadmap 入口 |
