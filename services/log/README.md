# services/log

ulog 自定义 backend：异步日志输出到 USB CDC。

- 实现：`ulog_cdc_be.c` / `ulog_cdc_be.h`
- 依赖：`services/usb`（`cdc_io.h`）、`ulog` 组件
- 注册：`INIT_COMPONENT_EXPORT(app_ulog_cdc_be_init)`
