# stream / 透传流程图

用法：[`README.md`](README.md)。静音：[`../log/README.md`](../log/README.md)。

```mermaid
flowchart TD
  PC["PC 同一 COM"] --> RX[CDC RX]
  RX --> CLI[cli 拼行]
  CLI --> J{JSON?}
  J -->|是 stream.set| M[mode_passthru_set 异步]
  M --> MODE[MODE 线程 PT_APPLY]
  MODE --> H[ulog_cdc_passthru_hold 1]
  H --> R[gnss/rdss_passthru_enter]
  J -->|否且已透传| DL[模块 UART TX]
  UART[模块 UART RX] --> SW[stream_write]
  SW --> SINK[stream_cdc_sink]
  SINK --> CDC[cdc_acm_write]
  MODE -->|flags=0 或失败| U[hold 0 恢复 ulog]
```

一次只开 gnss 或 rdss 一个通道。ON/CHARGE 才能进；ALARM/OFF 拒绝。  
串口助手：USB CDC、DTR、一行 JSON `stream.set`；关必须 `{` 开头。详见 [`README.md`](README.md)、[`docs/board_bringup.md`](../../docs/board_bringup.md) 第 0.3、7、8 节。
