# 三套库（互不包含业务）

> **变更记录（2026-08-18）**  
> BKP 载荷增加 `lb_sent`（本轮低电 N 是否已发，防 STOP2 重复）。STOP2 使用 `.rram` 栈，看门狗复位仍靠 BKP。

存储层只提供原始块，**不 include** `mode` / `cfg` / `session`。

流程图（含上电恢复）：[`flow.md`](flow.md)。

| 库 | 路径 | 容量 | 谁写 | 保什么 |
|----|------|------|------|--------|
| Flash 双槽 | `BSP/nvflash` | 末尾 2×2KB | 仅 `services/cfg` | 身份/配置 |
| BKP 用户区 | `BSP/bkp_user` | DAT3 起约 80B | 仅 `app/mode` | MODE + 告警锚点 + `lb_sent` |
| R-SRAM | `BSP/rram` | `0x20020000` 16KB | STOP2 栈 / 可选块 | STOP2 可保持 |

RTC 占用 **DAT1/DAT2**（magic / 已校时）。手册里 BKP 窗口 1KB 是外设地址，有效数据寄存器仍是 42×16 位。

## 谁在什么时候写

```text
cfg.set / 换北斗卡 / 首次定位
        → cfg 标 dirty → 线程 cfgnv → nvflash_save
          （内容相同跳过；擦写 pm_lock + 喂狗；不进 idle hook）

进 ALARM / OFF / FORCE_OFF / FAKE_OFF←ALARM
告警人工结束（长按退出）
ALARM 第一次锁节奏锚点（mode_alarm_anchor_latch）
进入 LOW_BATT 时置 `lb_sent`；回到 OK 清
        → mode 当场 bkp_user_save
          （FORCE_OFF 时 alarm_active=0，告警窗作废）
```

2/5/10min 节拍、`$BDICP` 卡号未变、电量、透传：**不写 Flash、不写 BKP**。  
例外：ALARM 第一次 GNSS 锁节奏锚点会写 BKP；校时写 RTC 日历 + DAT2。

## 复位 / 看门狗醒来（mode_init）

```text
保护且无 USB     → FORCE_OFF（告警作废）
BKP 告警有效 → 续 ALARM（含满 72h、从 FAKE_OFF←ALARM 醒来）
有 USB           → CHARGE
否则看电量       → 保护则 FORCE_OFF；OFF 且 WARN 且 lb_sent=0 则发 1 条 N
不恢复 ON（含 FAKE_OFF←ON）
```

真掉电（BKP 也没了）：身份仍从 Flash LOAD；MODE 按上电默认 + 电量，**不续告警**。

未校时（`rtc_get_unix()==0`）：复位续告警 elapsed=0。  
GNSS 校时：ON 第一次有效定位；ALARM 仅尚未 synced 时。RDSS 不校。见 `BSP/rtc/README.md`。

STOP2：`OFF`/`FORCE_OFF` 走 `pm_stop2_enter()`（`.rram` 栈，醒后复位）。看门狗仍靠 BKP。`lb_sent` 挡住低电 N 重复发。
