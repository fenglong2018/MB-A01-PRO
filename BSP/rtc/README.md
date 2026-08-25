# RTC

> **变更记录（2026-08-18）**  
> 10s WakeUp：有卡 ON 从开机起就开（灯），假关机继续用（灯+空载采电）。已在跑则不重装，保持 10s 相位。真关机 STOP2 停 10s。

硬件日历 RTC，时钟源 **LSE 32768Hz**（PC14/PC15）。

流程图：[`flow.md`](flow.md)。

## 开关

- `product_config.h` → `USE_RTC=1`
- `rtconfig.h` → `RT_USING_RTC`
- `Makefile` → `-DHSE_VALUE=8000000`（评估板 8M；产品板若是 32M 再改）
- 初始化：`INIT_ENV_EXPORT`（早于 `mode` 的 APP，保证上电能读 `synced`）

## 线程

校时线程 `rtc`（优先级 `RTC_THREAD_PRIO`，默认 20）。  
读时间 `rtc_get_unix()` **不经线程**，直接读日历；**未校时对外返回 0**（上电默认 2020-01-01 不当墙钟）。

## 解耦：消息校时

```text
session（产品） / CLI test.rtc
    │  rtc_post_unix(sec, src)   ← 非阻塞投递
    ▼
  sync mq
    ▼
  rtc 线程 → 写硬件日历（年/月/日/时/分/秒）→ BKP DAT2 synced
```

| API | 说明 |
|-----|------|
| `rtc_post_unix(sec, src)` | **唯一写入口**（`RTC_SRC_GNSS` / `CLI`；`RDSS` 预留不用） |
| `rtc_get_unix()` | UTC 秒；未 `rtc_is_synced()` 或失败 → **0** |
| `rtc_is_synced()` | 本上电或 BKP DAT2 已校准过 |
| 设备 `"rtc"` SET_TIME | 转 `rtc_post_unix(..., CLI)` |

GNSS **不要**直接写硬件，只把 RMC 合成 Unix 放在 `gnss_fix_t.unix_sec`。  
JSON `cfg` **不配**年月日。

## 产品何时 `post`（session）

短报文时刻来自 GNSS RMC，**不读 RTC**。RTC 只给告警 2/5/10min 节奏 / 复位续跑。

| 时机 | 写 RTC |
|------|--------|
| **ON**，本次进入后第一次 `unix_sec≠0` | 校 **一次**；其后 10min 不再写 |
| **ALARM** 且尚未 `synced` | 允许校 **一次**（可失败重试直到成功） |
| ALARM 已校过 / 透传 / RDSS / `test.gnss.fix` / `test.session.once` | **不写** |

ALARM 未校时：BKP `alarm_start_unix=0`，节奏一直 2min，不切 5min/10min。  
第一次 GNSS Unix 到来：`mode_alarm_anchor_latch` 把节奏锚点锁成该 Unix。

工厂/调试仍可用 `{"cmd":"test.rtc","unix":...}`，不当作出厂配置项。

## 时钟关系

| 晶振 | 用途 |
|------|------|
| HSE 8MHz | SYSCLK 144MHz（×18）；USB=PLL/3=48M |
| LSE 32768Hz | RTC 1Hz（127+255 分频） |

BKP：**DAT1/DAT2** 仅本模块（magic / synced）。MODE 粘性从 DAT3 起（`bkp_user`）。

## 文件

| 文件 | 说明 |
|------|------|
| `rtc.h` | 对外 API |
| `rtc_hw.c` | LSE、日历、mq、校时线程 |

## 低功耗（2026-08-18 已接）

- **RTC 10s WakeUp**：`rtc_wu_start(10)` / `rtc_wu_stop()`；ISR 只清标志、喂狗、调钩子。LED/session/MODE 各自注册，RTC **不 include** 它们。
- 假关机闪灯：LED 等 `LED_MSG_HB`，不靠 tick 空等 9.9s。
- 假关机采电：MODE 钩子只在 `FAKE_OFF` `request()` 1 次；WARN/PROTECT 由 ADC 边沿上报。
- 2/5/10min：session 在 WU 线程里用 **Unix** 判断到点；未校时才退回数 10s 拍。
- 真关机 STOP2：`pm_stop2_enter()`，见 `docs/low_power_stop2.md`。

详见 [`docs/low_power_stop2.md`](../../docs/low_power_stop2.md)。深睡前不关 LSE/RTC。
