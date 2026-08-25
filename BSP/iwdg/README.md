# BSP/iwdg

独立看门狗，**不**开 `RT_USING_WDT`（官方 `drv_wdt` 关狗时会关 LSI，且超时换算不可靠）。

流程图：[`flow.md`](flow.md)。无单独 CLI；看 `mode.get` 是否已离开 OFF。

> **变更记录（2026-08-18）**  
> 模块落地。非 OFF/FORCE_OFF 开启，超时≈26s；RTC 10s ISR / 发信 5s / nvflash 喂狗。STOP2 前 `iwdg_stop2_quiet`。

| API | 说明 |
|-----|------|
| `iwdg_start` | LSI/256、重装 `0xFFF` ≈ **26s**（>16s）。幂等 |
| `iwdg_feed` | 重装计数 |
| `iwdg_stop2_quiet` | 清 `IWDGWPEN/IWDGRSTEN`，避免 STOP2 被狗咬醒 |

谁喂：MODE 线程每 5s（CHARGE/ON 等已开狗时不能 `FOREVER` 挂死）、RTC 10s ISR、发信 5s、nvflash 擦写。

谁开：MODE 在非 OFF/FORCE_OFF 时 `iwdg_for_alive()`。冷启动真关机先**不开**，才能干净 STOP2（IWDG Enable 后硬件通常关不掉）。
