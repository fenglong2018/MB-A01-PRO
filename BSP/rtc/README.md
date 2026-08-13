# RTC

硬件日历 RTC，时钟源 **LSE 32768Hz**（PC14/PC15）。

## 开关

- `product_config.h` → `USE_RTC=1`
- `rtconfig.h` → `RT_USING_RTC`
- `Makefile` → `-DHSE_VALUE=32000000`

## 线程？

**有**：校时线程 `rtc`（优先级 `RTC_THREAD_PRIO`，默认 20）。  
硬件初始化 + 设备注册也在同模块 `INIT_APP_EXPORT` 完成。  
读时间 `rtc_get_unix()` **不经线程**，直接读日历寄存器。

## 解耦：消息校时

```text
GNSS / RDSS / CLI
    │  rtc_post_unix(sec, src)   ← 非阻塞投递
    ▼
  sync mq
    ▼
  rtc 线程 → 写硬件日历 → BKP synced 标记
```

| API | 说明 |
|-----|------|
| `rtc_post_unix(sec, src)` | **唯一校时入口**（`RTC_SRC_GNSS` / `RDSS` / `CLI`） |
| `rtc_get_unix()` | 读 UTC |
| `rtc_is_synced()` | 是否已校准过 |
| 设备 `"rtc"` SET_TIME | 内部也转 `rtc_post_unix(..., CLI)` |

GNSS/RDSS **不要**直接写硬件，只 `post`。

## 时钟关系

| 晶振 | 用途 |
|------|------|
| HSE 32MHz | SYSCLK 144MHz；USB=PLL/3 |
| LSE 32768Hz | RTC 1Hz（127+255 分频） |

## 文件

| 文件 | 说明 |
|------|------|
| `rtc.h` | 对外 API |
| `rtc_hw.c` | LSE、日历、mq、校时线程 |

## 低功耗（方案，未实施）

详见 [`docs/low_power_stop2.md`](../../docs/low_power_stop2.md)。

| 项 | 规划 |
|----|------|
| 角色 | 墙钟 + **Alarm/WakeUp** 作为 STOP2 主唤醒源（绝对 Unix，对齐 2/5/10min） |
| 校时 | 仍只走 `rtc_post_unix`；深睡前不关 LSE/RTC |
| BKP | 已有 synced；扩展与 `pm_ctx`（MODE / 告警锚点）共存布局时再定寄存器分配 |
| 注意 | STANDBY 勘误：周期 WakeUp 不可用，须 Alarm；本方案主路径为 **STOP2** |
