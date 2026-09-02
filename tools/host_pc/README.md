# shySOFT Host V0.2

Windows 上位机（**Python + PySide6**）：经 USB CDC 虚拟串口调试板端 JSON CLI。

> **变更记录（2026-08-27）**  
> IO 页接通板端 GPIO；`EN_5V`/`EN_PGNSS`/`EN_PRDSS` 按低有效勾选。  
> `WriteFile` /「设备不识别此命令」是 Windows USB CDC 没写出，不是 `EN_PLNA_POW` 未知；写失败会回退勾选。  
>
> **变更记录（2026-08-25）**  
> 射频页透传与串口助手对齐：通道状态（GNSS/RDSS）、开失败提示、一次只开一个（开 A 先关 B）、CHARGE/ON 才能进。  
> 配置含软硬件版本。**射频页**：透传开关 + 解析 GNSS GSV/GGA、RDSS `$BDPWI`（不另开两页）。

## 功能（V0.2）

| 页 | 内容 |
|----|------|
| 连接栏 | 端口刷新、打开时 **DTR=1**、Ping、自动监视 |
| 监视 | `mode.get` / `test.adc` / `test.rtc` / `test.pm` |
| 配置 | `cfg.get` / `cfg.set`（偏移、hw_ver）；sw_ver / 升级 Unix / 卡号 / 首次定位只读 |
| 自检 | 全部 `test.*` + RTC 校时 |
| **射频** | GNSS/RDSS 透传开关、ulog 静音、可见星/SNR、波束/S2C_d |
| IO | 勾选控制输出（低有效脚已取反）；输入只读 |
| 原始命令 | 手写一行 JSON |
| 日志区 | LOG / TX / RSP；默认可隐藏 `$` 行 |

协议见 [`../../services/cli/README.md`](../../services/cli/README.md)。上板 / **串口助手开关透传**：[`../../docs/board_bringup.md`](../../docs/board_bringup.md) 第 0.3、7、8 节。

## 射频页

不要为 GNSS、RDSS 各开一页（页签会挤）。一页左右分栏：

| | GNSS | RDSS |
|--|------|------|
| 开透传 | 点「GNSS 透传 开」→ `stream.set` gnss `enable:1` | 点「RDSS 透传 开」→ `stream.set` rdss |
| 关透传 | 点「关」→ `enable:0`（JSON `{` 开头，与串口助手相同） | 同左 |
| 解析 | `$GxGSV` 可见星 SNR；`$GxGGA` 使用数/质量 | `$BDPWI` 时间、波束号、S2C_d |
| 颜色 | SNR≥35 绿、≥20 黄、更低红 | S2C_d>40 绿（与固件门限一致） |

与串口助手是同一套命令、同一个 USB CDC。**不要**同时开串口助手占这个 COM。

- 波特率填 115200、打开时拉 DTR（连接按钮已做）。  
- 须 `CHARGE`（插 USB）或 `ON`；`OFF` / `ALARM` / 保护会提示未进入透传。  
- 一次只开一个：开 GNSS 若 RDSS 已开会先关 RDSS。  
- 默认「日志不刷 NMEA」：表格照更新，底部不被 1Hz 多句淹没。静音 ulog 仍建议开。  
- 测完点「关」，否则自检 `test.gnss.fix` / `test.rdss.*` 会 `busy_passthru`。

串口助手逐步操作见 [`docs/board_bringup.md`](../../docs/board_bringup.md) 第 0.3、7、8 节。

## 配置

写入带 `charge_offset_mv`、`adc_vdda_mv`；`hw_ver` 非空则写出厂版本。`sw_ver` 只读。连接后自动 `cfg.get`。

## 环境

```bat
cd tools\host_pc
py -3 -m pip install -r requirements.txt
py -3 -m shy_host
```

日常请双击 `run.bat`（用 `pythonw`，没有黑框/PowerShell）。在 Cursor 终端里跑 `py -3 -m shy_host` 时，那个窗口是终端本身，关掉等于退出上位机。

## 生成 EXE

双击 `build_exe.bat`，或：

```bat
cd tools\host_pc
py -3 -m pip install -r requirements.txt pyinstaller
py -3 -m PyInstaller --noconfirm --clean --windowed --name shySOFT_Host --collect-all PySide6 --paths . shy_host\main.py
```

生成目录：`tools\host_pc\dist\shySOFT_Host\shySOFT_Host.exe`（同目录的 dll 要一起拷）。第一次启动可能被 Defender 扫一会儿。`dist/`、`build/` 已加入 `.gitignore`，不要提交。
## 使用要点

1. 选 COM → 连接（DTR）→ Ping  
2. 射频页开 GNSS 或 RDSS 透传，看表（当前通道会显示 GNSS/RDSS）  
3. 自检 `test.gnss.fix` 等会阻塞，勿连发；透传未关会提示 `busy_passthru`  
4. IO 页不要当电源开关。若弹 `WriteFile failed` /「设备不识别此命令」：命令没到板子，断开重连或拔插 USB；`EN_PLNA_POW` 在白名单里（高开 PA8）  

## 目录

```text
tools/host_pc/shy_host/
├── main_window.py
├── radio_parse.py   # GSV / GGA / BDPWI
├── protocol.py
└── serial_link.py
```

## 版本

| 版本 | 说明 |
|------|------|
| V0.1 | MVP |
| **V0.2** | 静音、cfg 版本/偏移、射频解析表；2026-08-25 射频开/关与串口助手对齐 |
