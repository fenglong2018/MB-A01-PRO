# stream（数据流骨架）

统一管理 **cli / gnss / rdss** 逻辑通道：运行时开关、透传出口预留。  
与物理 USB 口数量无关；以后多 CDC 时只改 `stream_set_sink()` 绑定。

流程图：[`flow.md`](flow.md)。

> **变更记录（2026-08-25）**  
> 串口助手开/关见 [`docs/board_bringup.md`](../../docs/board_bringup.md) 第 0.3、7、8 节。  
> 透传时 MODE 静音 CDC ulog（见 [`../log/README.md`](../log/README.md)）。通道开关仍只走 `stream.set` → `mode_passthru_set`。

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

PC 侧用 `stream.list` / `stream.set` / `stream.get`（见 `services/cli/README.md`）。任意串口助手即可：USB CDC、打开 DTR、一行一条 JSON、`\n` 结尾。  
真正透传数据 **不要** 包进 JSON：模块 UART → `stream_write` → USB CDC（与 CLI 同一 COM）。PC 发非 `{` 行 → 模块 UART；以 `{` 开头的行仍是 CLI（用来 `stream.set` 退出）。  
进 `PASSTHRU` 后 CDC ulog 默认静音，COM 上主要是模块句；退出后日志恢复。需要透传时仍看 ulog：先 `log.cdc` `passthru_mute:0`。

```json
{"id":3,"cmd":"stream.set","name":"gnss","enable":1}
{"id":4,"cmd":"stream.set","name":"rdss","enable":1}
{"id":6,"cmd":"stream.set","name":"gnss","enable":0}
{"id":7,"cmd":"stream.set","name":"rdss","enable":0}
```

建议一次只开一个。`OFF` / `ALARM` / 保护不能进。两个通道都开过，须两个都 `enable:0` 才退出 `PASSTHRU`。

上板步骤见 [`docs/board_bringup.md`](../../docs/board_bringup.md) 第 0.3、7、8 节。

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
