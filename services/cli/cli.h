/**
 * @file cli.h
 * @brief USB CDC JSON 控制面入口与 cli.c 参数宏
 *
 * 用法见 services/cli/README.md
 * ulog：cli.c 先包含本头文件，再 #include <ulog.h>
 */
#ifndef __CLI_H__
#define __CLI_H__

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* cli.c 参数宏                                                               */
/* -------------------------------------------------------------------------- */
/** CDC 下行环形缓冲（字节） */
#define CLI_RX_RB_SIZE          512
/** 单行 JSON 最大长度（含结尾 0） */
#define CLI_LINE_MAX            256
/** 应答缓冲最大长度（含 GNSS 自检 JSON） */
#define CLI_RSP_MAX             768
/** CLI 处理线程栈（字节；gnss/rdss 自检会在本线程阻塞等待） */
#define CLI_THREAD_STACK        2560
/** CLI 处理线程优先级（数值越小越高） */
#define CLI_THREAD_PRIO         16

/* cli 模块 ulog（须在 #include <ulog.h> 之前生效；7=DBG 6=INFO 4=W 3=E） */
#define LOG_TAG                 "cli"
#define LOG_LVL                 6

/**
 * CLI 初始化。USE_CLI 时由 INIT_APP_EXPORT(app_cli_init) 自动调用。
 */
int cli_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __CLI_H__ */
