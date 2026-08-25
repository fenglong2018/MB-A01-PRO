# KEY

四路 IO（SOS / USB / FALL / SIM）：EXTI → 禁中断 → key 线程 10ms 轮询滤波 → rearm。

> **变更记录（2026-08-24）**  
> 滤波期间 `pm_lock`：STOP0 会停 SysTick，10ms 轮询和长按计数都会冻住。
>
> **变更记录（2026-08-21）**  
> STOP2 唤醒 = 软件复位，那个 EXTI 沿丢在复位里，只等沿会把整次按下丢掉（现象：按 SOS 只有 BOOT0/LED3 闪一下就又睡了）。现在复位最早期锁存 SOS/FALL 电平并接管滤波；长按阈值由 160ms 改为 **2s**。细节见 [`flow.md`](flow.md) 第 0.1 节。
>
> **变更记录（2026-08-18）**  
> 按键语义未改。MODE 侧：告警短按只看电；OFF 冷启动 WARN 发低电 N 见 `fsm.md`。

- **逻辑与流程图（请确认）**：[`flow.md`](flow.md)（滤波/USB 双沿/复位接管）
- 事件进 MODE 之后的按键语义（告警短按看电、FAKE_OFF 等）：[`app/mode/fsm.md`](../../app/mode/fsm.md)
- 对外入口：`key_init()`、`sim_present()`、`key_is_busy()`
- 底层 EXTI：`board_key_irq.c`（随本目录编译；声明在 `board_key_irq.h`）
