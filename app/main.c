/**
 * @file main.c
 * @brief 应用入口：业务 init 已由 INIT_*_EXPORT 自动完成
 */
#include <rtthread.h>
#include "config.h"
#include "main.h"
#include <ulog.h>

int main(void)
{
    LOG_I("app running (inits via INIT_*_EXPORT)");
#if USE_CLI
    LOG_I("CLI JSON: see services/cli/README.md");
#endif

    while (1)
    {
        rt_thread_delay(50);
    }
}

