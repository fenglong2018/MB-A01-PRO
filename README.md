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
│   ├── cfg/                 # 身份/配置（Flash 双槽）
│   ├── stream/              # 透传抽象
│   ├── log/                 # ulog → CDC backend
│   └── usb/                 # CDC ACM / usb_app
│
├── board/                   # 仅板级 init（时钟/引脚/USB HW/KEY EXTI）
│   ├── board.c / usb_hw.c / n32wb452_it.c
│   ├── board_key_irq.*      # KEY EXTI（引脚宏来自 config/board_pins.h）
│
├── BSP/                     # 用户板级模块（key/led/nvflash/bkp_user/rram…）
├── DeviceDrivers/           # 芯片适配（drv_gpio/uart…，换芯片才改）
├── firmware/                # CMSIS + StdPeriph（只读）
└── middlewares/rt-thread/   # 内核（只读；CherryUSB 暂仍在其下）
```

依赖：`app → services/BSP → board/DeviceDrivers → firmware`；`middlewares` 默认不改。

## 调试相关文档

| 主题 | 文档 |
|------|------|
| USB JSON 命令（ping/io/log/stream/test） | [services/cli/README.md](services/cli/README.md) |
| **上位机 V0.2（Python+PySide6）** | [tools/host_pc/README.md](tools/host_pc/README.md) |
| GNSS/RDSS 透传骨架 | [services/stream/README.md](services/stream/README.md) |
| CDC ulog（透传静音） | [services/log/README.md](services/log/README.md) |
| GNSS 占位 | [BSP/gnss/README.md](BSP/gnss/README.md) |
| RTC 墙钟 / GNSS 校时 | [BSP/rtc/README.md](BSP/rtc/README.md) |
| RDSS 占位 | [BSP/rdss/README.md](BSP/rdss/README.md) |
| 电源/报文 roadmap | [docs/roadmap_power_msg.md](docs/roadmap_power_msg.md) |
| **整机软件说明（架构/API/调用关系）** | [docs/software_manual.md](docs/software_manual.md) |
| **模块完成度 / 流程图总表** | [docs/module_status.md](docs/module_status.md) |
| 三套库（Flash / BKP / R-SRAM） | [BSP/nvflash/README.md](BSP/nvflash/README.md) |
| 低功耗 STOP0 浅睡（idle hook） | [BSP/pm/README.md](BSP/pm/README.md) |
| 低功耗 STOP2 / RTC 10s / IWDG（2026-08-18 已接代码） | [docs/low_power_stop2.md](docs/low_power_stop2.md) |
| 整机状态机 | [app/mode/fsm.md](app/mode/fsm.md) |
| **板级按模块验证（CLI/上位机）** | [docs/board_bringup.md](docs/board_bringup.md) |

### 变更记录（2026-08-18）

- `FAKE_OFF`：RTC 10s 浅醒闪灯，并空载采 ADC 1 次。
- `LOW_BATT`：OFF/看电/ON 可发 1 条 N；告警不插；冷启动 WARN 发 1 条，BKP `lb_sent` 防 STOP2 重复发。
- 真关机 `OFF`/`FORCE_OFF` 走 STOP2；硬件 IWDG 非真关机开启（≈26s）。

## 自动初始化（INIT_*_EXPORT）

| 等级 | 符号 | 文件 |
|------|------|------|
| BOARD `1` | `rt_hw_pin_init` / `rt_hw_usart_init` | `DeviceDrivers/...` |
| PREV `2` | `ulog_init` | 官方 ulog |
| DEVICE `3` | `app_usb_cdc_init` | `services/usb/cdc_acm.c` |
| COMPONENT `4` | `app_stream_init` / `app_ulog_cdc_be_init` | `stream` / `log` |
| ENV `5` | `app_cfg_init` / `app_adc_bat_init` / `app_rram_init` / `app_rtc_init` | `cfg` / `adc` / `rram` / `rtc` |
| APP `6` | `app_cfg_nv_thread`、`mode`、`key`、`led`、`gnss`、`rdss`、`pm`、`cli` | 各模块 |

`main()` 只跑业务循环。新增模块按依赖选 `INIT_DEVICE/COMPONENT/ENV/APP_EXPORT`。

## 宏放哪里

与 `.c` 同名的 `.h`；产品开关与整板 IO 在 `config/`（如 `product_config.h`、`board_pins.h`）。

## 编译（WSL）

```bash
cd /mnt/e/2026星海/shySOFT/v0.2/gcc-rt-cursor
make            # 增量编译（默认 -j12）
make clean      # 清 build
make flash      # 烧录已有 hex（缺则先编）
```

## 调试（Cursor + J-Link）

板载 **USB CDC** 和 **J-Link SWD（评估板 J12）** 是两路：CDC 给上位机/日志，J-Link 给烧录和下断点。

**Cursor 插件：** C/C++、Cortex-Debug、Remote-WSL；上位机再加 Python。

**推荐：在 WSL 里打开仓库再 F5（配置 `J-Link (WSL)`）**

1. Windows 装 [usbipd-win](https://github.com/dorssel/usbipd-win/releases)，管理员 PowerShell：`usbipd list` → `bind` → `attach --wsl --busid <BUSID>`  
2. Ubuntu 装交叉工具链、GDB、Linux J-Link：`sudo apt install gcc-arm-none-eabi gdb-multiarch`，J-Link 从 SEGGER 下 Linux 包（`JLinkExe` / `JLinkGDBServer`）  
3. WSL 里 `lsusb` 能看到 SEGGER，`make -j12` 生成 `build/User.elf`  
4. Cursor：命令面板 **WSL: Reopen Folder in WSL** → 运行和调试选 **J-Link (WSL)** → F5  

在 Windows 里 F5 用 **J-Link (Windows)**：不要把枪 attach 给 WSL，本机装 SEGGER J-Link；`build/User.elf` 仍要先在 WSL 编出来。J-Link 不能同时给 Windows 和 Ubuntu 占用。
