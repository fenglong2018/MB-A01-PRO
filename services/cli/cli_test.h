/**
 * @file cli_test.h
 * @brief CLI 调试/自检命令（mode.get / test.*）
 */
#ifndef __CLI_TEST_H__
#define __CLI_TEST_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 处理 test.* / mode.get 等调试命令。
 * @return 1=已处理并写入 rsp；0=不是本模块命令；<0 内部错误（少见）
 */
int cli_test_handle(const char *cmd, const char *line, int id, char *rsp, int rsp_size);

#ifdef __cplusplus
}
#endif

#endif /* __CLI_TEST_H__ */
