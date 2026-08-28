# SSCOM 快捷命令

文件：[`sscom_cli.ini`](sscom_cli.ini)（SSCOM 5.13「扩展 / 多字符串」导入格式）。

## 导入

1. 打开 SSCOM → 点 **扩展**，出现多字符串列表。
2. 点 **导入**，选本目录 `sscom_cli.ini`。
3. 若按钮上中文乱码：用记事本打开 ini → **另存为**，编码选 **ANSI**，再导入。
4. 勾选 **加回车换行**、打开 **DTR**，波特率 115200。不要和 `shySOFT Host` 同时占这个 COM。

点右侧按钮单条发送。左侧 HEX **不要勾**（JSON 是文本）。

「顺序」列：`0` 不参与循环；`1/2/3` 仅在勾选循环发送时按号发出。循环不要带着 GNSS/RDSS 开透传。

细则：[`docs/board_bringup.md`](../../docs/board_bringup.md) 第 0.3、7、8 节。
