# LED 流程图

极性：低电平点亮。用法：`{"cmd":"test.led","mode":4,"pct":50}`。MODE 推的是**视图**（FAKE_OFF←ALARM→ALARM；FAKE_OFF←ON 有卡→ON / 无卡→OFF；PASSTHRU+USB→CHARGE）。

```mermaid
flowchart TD
  A[led 线程] --> B{消息}
  B -->|PERCENT| C[更新 pct]
  B -->|MODE 视图| D[切动画]
  B -->|HB RTC 10s| E[ALARM 假关机双闪；有卡 ON 假关机单闪]
  D --> F{视图}
  F -->|OFF FORCE_OFF| G[全灭]
  F -->|ON LOW_BATT| H[约 10s 三灯 100ms]
  F -->|ON 无卡提示| H2[5s 内每秒 100ms]
  F -->|ALARM| I[10s 双闪]
  F -->|BATT| J[流水后保持最后一档到 5s]
  F -->|CHARGE| K{pct 大于等于 98?}
  K -->|是| L[三灯常亮]
  K -->|否| M[流水循环 档 30/70]
```

`FAKE_OFF` 下 SysTick 会停，脉冲靠 `LED_MSG_HB`，不要靠软件空等 9.9s。
