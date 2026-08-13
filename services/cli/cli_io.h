#ifndef __CLI_IO_H__
#define __CLI_IO_H__

#ifdef __cplusplus
extern "C" {
#endif

int cli_io_list_json(char *buf, int buflen);
int cli_io_get(const char *pin, int *val);
int cli_io_set(const char *pin, int val);

#ifdef __cplusplus
}
#endif

#endif /* __CLI_IO_H__ */
