# BSP/rram

片内 Retention RAM：`0x20020000`～`0x20023FFF`（16KB）。

流程图：[`../nvflash/flow.md`](../nvflash/flow.md)（STOP2 栈）。无 CLI。

> **变更记录（2026-08-18）**  
> STOP2 入口把栈切到 `.rram`（`pm_stop2_enter`），并开 PWR retention。本模块 CRC 块 API 仍无业务写入。

- STOP2 可保持（PWR retention 已在 STOP2 入口打开）
- 看门狗 / POR 一般丢失
- 链接脚本已单独 `RETRAM` + `.rram`；堆止于 `0x20020000`
