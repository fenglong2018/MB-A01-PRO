# shySOFT Host V0.2

Windows 上位机（**Python + PySide6**）：经 USB CDC 虚拟串口调试板端 JSON CLI。

> **变更记录（2026-08-19）**  
> 配置含软硬件版本。**射频页**：透传开关 + 解析 GNSS GSV/GGA、RDSS `$BDPWI`（不另开两页）。

## 功能（V0.2）

| 页 | 内容 |
|----|------|
| 连接栏 | 端口刷新、打开时 **DTR=1**、Ping、自动监视 |
| 监视 | `mode.get` / `test.adc` / `test.rtc` / `test.pm` |
| 配置 | `cfg.get` / `cfg.set`（偏移、hw_ver）；sw_ver / 升级 Unix / 卡号 / 首次定位只读 |
| 自检 | 全部 `test.*` + RTC 校时 |
| **射频** | GNSS/RDSS 透传开关、ulog 静音、可见星/SNR、波束/S2C_d |
| IO | 勾选控制输出；板端未接则 `not_ready` |
| 原始命令 | 手写一行 JSON |
| 日志区 | LOG / TX / RSP；默认可隐藏 `$` 行 |

协议见 [`../../services/cli/README.md`](../../services/cli/README.md)。上板：[`../../docs/board_bringup.md`](../../docs/board_bringup.md)。

## 射频页

不要为 GNSS、RDSS 各开一页（页签会挤）。一页左右分栏：

| | GNSS | RDSS |
|--|------|------|
| 开透传 | `stream.set` gnss | `stream.set` rdss |
| 解析 | `$GxGSV` 可见星 SNR；`$GxGGA` 使用数/质量 | `$BDPWI` 时间、波束号、S2C_d |
| 颜色 | SNR≥35 绿、≥20 黄、更低红 | S2C_d>40 绿（与固件门限一致） |

默认「日志不刷 NMEA」：表格照更新，底部不被 1Hz 多句淹没。静音 ulog 仍建议开，以免插进半句。

一次只开一个通道。退出：点「关」（JSON `{` 开头）。

## 配置

写入带 `charge_offset_mv`；`hw_ver` 非空则写出厂版本。`sw_ver` 只读。连接后自动 `cfg.get`。

## 环境

```bat
cd tools\host_pc
py -3 -m pip install -r requirements.txt
py -3 -m shy_host
```

## 使用要点

1. 选 COM → 连接（DTR）→ Ping  
2. 射频页开 GNSS 或 RDSS 透传，看表  
3. 自检 `test.gnss.fix` 等会阻塞，勿连发  
4. IO 页不要当电源开关  

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
| **V0.2** | 静音、cfg 版本/偏移、射频解析表 |
