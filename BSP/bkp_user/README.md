# BSP/bkp_user

备份域用户数据（`BKP_DAT3` 起）。**DAT1/DAT2 仅 RTC 使用。**

> **变更记录（2026-08-18）**  
> MODE 粘性增加 `lb_sent`（本轮低电 N 已发）。STOP2 软件复位后仍在；真掉电丢失。本模块仍不解释字段。

有效载荷约 80 字节 + 本模块 CRC。不解释 MODE。调用方：`app/mode`。流程图：[`../nvflash/flow.md`](../nvflash/flow.md)。

看门狗 / STOP2 复位后仍在；真掉电丢失。2/5/10min 节拍**不要**写这里。身份不放本库，见 `BSP/nvflash/README.md`。
