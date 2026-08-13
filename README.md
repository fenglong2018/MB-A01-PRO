# gcc-rt-cursor

N32WB452 + RT-Thread USB CDC（GCC / WSL Ubuntu）。

## 目录架构

```text
gcc-rt-cursor/
├── .vscode/                 # VS Code / WSL IntelliSense（仓库根）
├── Makefile                 # 唯一构建入口
├── tools/                   # J-Link、辅助脚本、host_pc 上位机
├── build/                   # 编译产物
│
├── config/                  # 产品宏、rtconfig、stream/cdc 配置
│   ├── product_config.h
│   ├── rtconfig.h
│   ├── stream_ports.h / cdc_acm.h
│   └── board_pins.h         # 整板 IO / 引脚定义
│
├── app/                     # 薄：业务编排
│   ├── main.c / main.h
│   ├── mode/                # 预留：模式机
│   └── session/             # 预留：GNSS/RDSS 等会话
│
├── services/                # 长期服务
│   ├── cli/                 # JSON 控制面
│   ├── stream/              # 透传抽象
│   ├── log/                 # ulog → CDC backend
│   └── usb/                 # CDC ACM / usb_app
│
├── board/                   # 仅板级 init（时钟/引脚/USB HW/KEY EXTI）
│   ├── board.c / usb_hw.c / n32wb452_it.c
│   ├── board_key_irq.*      # KEY EXTI（引脚宏来自 config/board_pins.h）
│
├── BSP/                     # 用户板级模块（key/led…，同目录 .c+.h），供 app/main 调用
├── DeviceDrivers/           # 芯片适配（drv_gpio/uart…，换芯片才改）
├── firmware/                # CMSIS + StdPeriph（只读）
└── middlewares/rt-thread/   # 内核（只读；CherryUSB 暂仍在其下）
```

依赖：`app → services/BSP → board/DeviceDrivers → firmware`；`middlewares` 默认不改。

## 调试相关文档

| 主题 | 文档 |
|------|------|
| USB JSON 命令（ping/io/log/stream/test） | [services/cli/README.md](services/cli/README.md) |
| **上位机 V0.1（Python+PySide6）** | [tools/host_pc/README.md](tools/host_pc/README.md) |
| GNSS/RDSS 透传骨架 | [services/stream/README.md](services/stream/README.md) |
| GNSS 占位 | [BSP/gnss/README.md](BSP/gnss/README.md) |
| RDSS 占位 | [BSP/rdss/README.md](BSP/rdss/README.md) |
| 电源/报文 roadmap | [docs/roadmap_power_msg.md](docs/roadmap_power_msg.md) |
| **整机软件说明（架构/API/调用关系）** | [docs/software_manual.md](docs/software_manual.md) |
| 低功耗 STOP0 浅睡（idle hook） | [BSP/pm/README.md](BSP/pm/README.md) |
| 低功耗 STOP2 + 假关机（预留空） | [docs/low_power_stop2.md](docs/low_power_stop2.md) |

## 自动初始化（INIT_*_EXPORT）

| 等级 | 符号 | 文件 |
|------|------|------|
| BOARD `1` | `rt_hw_pin_init` / `rt_hw_usart_init` | `DeviceDrivers/...` |
| PREV `2` | `ulog_init` | 官方 ulog |
| DEVICE `3` | `app_usb_cdc_init` | `services/usb/cdc_acm.c` |
| COMPONENT `4` | `app_stream_init` / `app_ulog_cdc_be_init` | `stream` / `log` |
| APP `6` | `app_cli_init` / `app_key_init` | `cli` / `BSP/key` |

`main()` 只跑业务循环。新增模块按依赖选 `INIT_DEVICE/COMPONENT/APP_EXPORT`。

## 宏放哪里

与 `.c` 同名的 `.h`；产品开关与整板 IO 在 `config/`（如 `product_config.h`、`board_pins.h`）。

## 编译（WSL）

```bash
cd /mnt/e/2026星海/shySOFT/v0.2/gcc-rt-cursor
make
make flash
```
