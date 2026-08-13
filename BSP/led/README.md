# BSP/led — 电量指示

## 硬件极性

LED1 / LED2 / LED3：**低电平点亮**。软件动画逻辑仍用「1=亮、0=灭」，仅在写 GPIO 时取反。

## 输入（消息队列）

| 消息 | 来源 | API |
|------|------|-----|
| `LED_MSG_PERCENT` | ADC | `led_post_percent()` |
| `LED_MSG_MODE` | MODE | `led_post_mode()` |

## 显示规则

| MODE | 行为 |
|------|------|
| `OFF` / `FORCE_OFF` | 全灭 |
| `ON` | 每 10s 三灯同亮 200ms，其余灭 |
| `ALARM` | 每 5s 三灯同亮 100ms，其余灭 |
| `BATT` | 流水 **仅 1 轮**，然后灭（MODE 5s 窗口内不再重复） |
| `CHARGE` | 流水循环；电量 **≥98%** 三灯常亮 |

### 流水（档位 30% / 70%）

- &lt;30%：LED1 300ms → 灭  
- &lt;70%：LED1 300ms → LED1+2 300ms → 灭  
- ≥70%：LED1 → LED1+2 → 三灯（各 300ms）→ 灭  

充电流水轮间间隔 300ms。
