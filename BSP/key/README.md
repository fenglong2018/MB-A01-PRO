# KEY

四路 IO（SOS / USB / FALL / SIM）：EXTI → 禁中断 → key 线程 10ms 轮询滤波 → rearm。

- **逻辑与流程图（请确认）**：[`flow.md`](flow.md)
- 对外入口：`key_init()`、`sim_present()`
- 底层 EXTI：`board_key_irq.c`（随本目录编译；声明在 `board_key_irq.h`）
