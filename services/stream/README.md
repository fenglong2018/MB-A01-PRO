# stream（数据流骨架）

统一管理 **cli / gnss / rdss** 逻辑通道：运行时开关、透传出口预留。  
与物理 USB 口数量无关；以后多 CDC 时只改 `stream_set_sink()` 绑定。

## 通道定义

`config/stream_ports.h`：

| name | ch | 现状 |
|------|----|------|
| `cli` | 0 | 已建成（控制面，非透传数据） |
| `gnss` | 1 | `USE_GNSS==0` 时 `built:0` |
| `rdss` | 2 | `USE_RDSS==0` 时 `built:0` |

## API（`stream.h`）

```c
stream_init();
stream_set_enable("gnss", 1);     /* 未编译则 not_built */
stream_is_built("gnss");
stream_is_enabled("gnss");
stream_set_sink("gnss", my_write);/* 注册透传到主机的出口 */
stream_write("gnss", buf, len);   /* 驱动 RX 后调用 */
stream_list_json(buf, buflen);
```

错误码宏：`STREAM_OK` / `STREAM_ERR_NOT_BUILT` / `STREAM_ERR_DISABLED` / `STREAM_ERR_NO_SINK` 等，见 `stream.h`。

## 与 JSON CLI 的关系

PC 侧用 `stream.list` / `stream.set` / `stream.get`（见 `services/cli/README.md`）。  
真正透传数据 **不要** 包进 JSON，应走 `stream_write` → sink（独立 CDC 或帧通道）。

## GNSS / RDSS 接入步骤（预留）

1. `product_config.h` 打开 `USE_GNSS` / `USE_RDSS`  
2. 在驱动里实现串口收包  
3. `stream_set_sink("gnss", ...)` 接到 USB 出口  
4. 收包回调里 `stream_write("gnss", data, len)`  
5. 用 `stream.set` 打开 enable 后再透传  

## 文件

| 文件 | 作用 |
|------|------|
| `stream.c` / `stream.h` | 实现与错误码 |
| `config/stream_ports.h` | 通道名 / ch |
