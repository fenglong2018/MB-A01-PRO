# shySOFT Host V0.1

Windows 上位机（**Python + PySide6**）：经 USB CDC 虚拟串口调试板端 JSON CLI。

## 功能（V0.1）

| 页 | 内容 |
|----|------|
| 连接栏 | 端口刷新、打开时 **DTR=1**、Ping、自动监视 |
| 监视 | `mode.get` / `test.adc` / `test.rtc` / `test.pm` |
| 配置 | `cfg.get` / `cfg.set` |
| 自检 | 全部 `test.*` + RTC 校时 |
| 透传 | `stream.list/get/set`（开关通道） |
| IO | 勾选控制输出脚（`io.set`）；LED1/2/3 低电平点亮（勾选→val=0） |
| 原始命令 | 手写一行 JSON |
| 日志区 | 区分 LOG / TX / RSP，可保存文件 |

协议见 [`../../services/cli/README.md`](../../services/cli/README.md)。

## 环境

- Windows 10/11
- Python 3.10+（建议 3.11/3.12）
- 板端已烧录且 `USE_CLI=1`，插 USB 出现 COM 口

```bat
cd tools\host_pc
py -3 -m pip install -r requirements.txt
py -3 -m shy_host
```

或双击 `run.bat`（优先用 Windows `py` 启动器）。

## 使用要点

1. 选 COM → **连接**（自动拉 DTR）→ 点 Ping 应见 `ok:1`
2. 日志区：绿色为 RSP，蓝色为 TX，灰色为设备 ulog
3. `test.gnss.fix` / `test.rdss.*` / `test.session.once` 板端阻塞较久，等待中勿连发
4. CDC 波特率多数情况下可忽略，默认 115200 即可

## 目录

```text
tools/host_pc/
├── README.md
├── requirements.txt
├── run.bat
└── shy_host/
    ├── __init__.py      # 版本号 V0.1
    ├── __main__.py
    ├── main.py
    ├── protocol.py
    ├── serial_link.py
    └── main_window.py
```

## 版本

| 版本 | 说明 |
|------|------|
| **V0.1** | MVP：连接+日志+监视+cfg+自检+stream/IO 骨架 |

后续可加：透传终端窗、电量曲线、一键自检报告导出。
