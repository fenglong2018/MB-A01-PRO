# BSP/ble — 充电时蓝牙

> **2026-08-31**  
> 对照官方 `examples/BLE/slave`（`transfer_and_receive`）。插 USB 脚开 `MCU_EN_BLE` 并初始化 Nations host；拔掉关射频。  
> CLI 应答回发起方；透传只给 `stream.set` 的那一路。  
> GATT：Service `0xFEE7`，Write/Notify `0xFEC1`（与官方 UKEY 相同）。nRF Connect 写一行 JSON 即可（不必 NS_BlueTooth 的 2 字节长度头）。

## 规则

| 事件 | 行为 |
|------|------|
| USB 插入（含告警中） | `ble_start()`，拉高 EN_BLE，广播 |
| USB 拔出 | `ble_stop()`；若透传发起方是 BLE，一并关透传 |
| JSON | 两路都能发，RSP 回发起方 |
| 透传上行/下行 | 只给/只收发起方；`enable:0` 哪一路都能关 |

广播名：`MBA01-xxxx`（`device_id` 后 4 位）。安卓调试 App：[`tools/android/README.md`](../../tools/android/README.md)。

## CLI

```json
{"id":1,"cmd":"ble.get"}
```

插着 USB 且栈起来后 `on=1`、`stack=1`。用 nRF Connect 搜 `MBA01-`，连上后对 `0xFEC1` 写 `{"cmd":"ping"}\n`。

## 文件

`ble.c` / `ble.h`、`bsp_timer.c`（TIM3，host 10ms）、`n32wb452_data_fifo.c`。库：`firmware/n32wb452_ble_driver/lib/IAR/host.a`、`n32wb452_ble.a`。开关在 `app/mode` 跟 USB 脚走。不要抄例程 USART1/PA9。
