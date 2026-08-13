/**
 * @file cli_json.h
 * @brief JSON 命令解析接口与 cli_json.c 参数宏
 */
#ifndef __CLI_JSON_H__
#define __CLI_JSON_H__

#ifdef __cplusplus
extern "C" {
#endif

/** cli_json / cli_test 临时拼装缓冲 */
#define CLI_JSON_TMP_SIZE       512
/** JSON 字段缓存长度 */
#define CLI_JSON_CMD_MAX        32
#define CLI_JSON_NAME_MAX       24
#define CLI_JSON_PIN_MAX        24
#define CLI_JSON_TAG_MAX        24

/** 处理一行 JSON 请求，结果写入 rsp（含结尾 \\0），返回写入长度 */
int cli_json_handle_line(const char *line, char *rsp, int rsp_size);

#ifdef __cplusplus
}
#endif

#endif /* __CLI_JSON_H__ */
