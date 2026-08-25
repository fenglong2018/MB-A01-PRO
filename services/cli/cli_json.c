/**
 * @file cli_json.c
 * @brief 轻量 JSON 命令分发（骨架，不依赖 cJSON）
 */
#include "cli_json.h"
#include "cli_io.h"
#include "cli_test.h"
#include "stream.h"
#include "cfg.h"

#include <rtthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef RT_USING_ULOG
#include <ulog.h>
#endif
#include "ulog_cdc_be.h"

static const char *json_find_key(const char *json, const char *key)
{
    char pattern[40];
    const char *p;

    if ((json == NULL) || (key == NULL))
    {
        return NULL;
    }
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(json, pattern);
    if (p == NULL)
    {
        return NULL;
    }
    p = strchr(p + strlen(pattern), ':');
    if (p == NULL)
    {
        return NULL;
    }
    return p + 1;
}

static int json_get_string(const char *json, const char *key, char *out, int out_sz)
{
    const char *p = json_find_key(json, key);
    int i = 0;

    if ((p == NULL) || (out == NULL) || (out_sz < 2))
    {
        return -1;
    }
    while (*p == ' ' || *p == '\t')
    {
        p++;
    }
    if (*p != '"')
    {
        return -1;
    }
    p++;
    while (*p && *p != '"' && i < (out_sz - 1))
    {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return (i > 0) ? 0 : -1;
}

static int json_get_int(const char *json, const char *key, int *out)
{
    const char *p = json_find_key(json, key);

    if ((p == NULL) || (out == NULL))
    {
        return -1;
    }
    while (*p == ' ' || *p == '\t')
    {
        p++;
    }
    *out = atoi(p);
    return 0;
}

static int rsp_ok(char *rsp, int rsp_size, int id, const char *extra_json)
{
    if (extra_json && extra_json[0])
    {
        return snprintf(rsp, (size_t)rsp_size,
                        "{\"type\":\"rsp\",\"id\":%d,\"ok\":1,%s}\r\n", id, extra_json);
    }
    return snprintf(rsp, (size_t)rsp_size,
                    "{\"type\":\"rsp\",\"id\":%d,\"ok\":1}\r\n", id);
}

static int rsp_err(char *rsp, int rsp_size, int id, const char *err)
{
    return snprintf(rsp, (size_t)rsp_size,
                    "{\"type\":\"rsp\",\"id\":%d,\"ok\":0,\"err\":\"%s\"}\r\n",
                    id, (err != NULL) ? err : "error");
}

int cli_json_handle_line(const char *line, char *rsp, int rsp_size)
{
    char cmd[CLI_JSON_CMD_MAX];
    char name[CLI_JSON_NAME_MAX];
    char pin[CLI_JSON_PIN_MAX];
    char tag[CLI_JSON_TAG_MAX];
    char tmp[CLI_JSON_TMP_SIZE];
    int id = 0;
    int enable = 0;
    int val = 0;
    int lvl = 0;
    int ret;

    if ((line == NULL) || (rsp == NULL) || (rsp_size < 32))
    {
        return -1;
    }

    (void)json_get_int(line, "id", &id);

    if (json_get_string(line, "cmd", cmd, sizeof(cmd)) != 0)
    {
        return rsp_err(rsp, rsp_size, id, "no_cmd");
    }

    if (strcmp(cmd, "ping") == 0)
    {
        return rsp_ok(rsp, rsp_size, id, "\"pong\":1");
    }

    if (strcmp(cmd, "stream.list") == 0)
    {
        ret = stream_list_json(tmp, (int)sizeof(tmp));
        if (ret < 0)
        {
            return rsp_err(rsp, rsp_size, id, "list_fail");
        }
        return snprintf(rsp, (size_t)rsp_size,
                        "{\"type\":\"rsp\",\"id\":%d,\"ok\":1,\"streams\":%s}\r\n", id, tmp);
    }

    if (strcmp(cmd, "stream.set") == 0)
    {
        if (json_get_string(line, "name", name, sizeof(name)) != 0)
        {
            return rsp_err(rsp, rsp_size, id, "no_name");
        }
        if (json_get_int(line, "enable", &enable) != 0)
        {
            return rsp_err(rsp, rsp_size, id, "no_enable");
        }
        ret = stream_set_enable(name, enable);
        if (ret != STREAM_OK)
        {
            return rsp_err(rsp, rsp_size, id, stream_err_str(ret));
        }
        snprintf(tmp, sizeof(tmp), "\"name\":\"%s\",\"enable\":%d,\"built\":1", name, enable ? 1 : 0);
        return rsp_ok(rsp, rsp_size, id, tmp);
    }

    if (strcmp(cmd, "stream.get") == 0)
    {
        int built;
        int en;

        if (json_get_string(line, "name", name, sizeof(name)) != 0)
        {
            return rsp_err(rsp, rsp_size, id, "no_name");
        }
        built = stream_is_built(name);
        if (built < 0)
        {
            return rsp_err(rsp, rsp_size, id, stream_err_str(built));
        }
        en = stream_is_enabled(name);
        if (en == STREAM_ERR_NOT_BUILT)
        {
            snprintf(tmp, sizeof(tmp), "\"name\":\"%s\",\"built\":0,\"enable\":0", name);
            return rsp_ok(rsp, rsp_size, id, tmp);
        }
        if (en < 0)
        {
            return rsp_err(rsp, rsp_size, id, stream_err_str(en));
        }
        snprintf(tmp, sizeof(tmp), "\"name\":\"%s\",\"built\":1,\"enable\":%d", name, en);
        return rsp_ok(rsp, rsp_size, id, tmp);
    }

    if (strcmp(cmd, "io.list") == 0)
    {
        ret = cli_io_list_json(tmp, (int)sizeof(tmp));
        if (ret < 0)
        {
            return rsp_err(rsp, rsp_size, id, "list_fail");
        }
        return snprintf(rsp, (size_t)rsp_size,
                        "{\"type\":\"rsp\",\"id\":%d,\"ok\":1,\"ios\":%s}\r\n", id, tmp);
    }

    if (strcmp(cmd, "io.get") == 0)
    {
        if (json_get_string(line, "pin", pin, sizeof(pin)) != 0)
        {
            return rsp_err(rsp, rsp_size, id, "no_pin");
        }
        ret = cli_io_get(pin, &val);
        if (ret == -1)
        {
            return rsp_err(rsp, rsp_size, id, "unknown_pin");
        }
        if (ret == -2)
        {
            return rsp_err(rsp, rsp_size, id, "not_ready");
        }
        snprintf(tmp, sizeof(tmp), "\"pin\":\"%s\",\"val\":%d", pin, val);
        return rsp_ok(rsp, rsp_size, id, tmp);
    }

    if (strcmp(cmd, "io.set") == 0)
    {
        if (json_get_string(line, "pin", pin, sizeof(pin)) != 0)
        {
            return rsp_err(rsp, rsp_size, id, "no_pin");
        }
        if (json_get_int(line, "val", &val) != 0)
        {
            return rsp_err(rsp, rsp_size, id, "no_val");
        }
        ret = cli_io_set(pin, val);
        if (ret == -1)
        {
            return rsp_err(rsp, rsp_size, id, "unknown_pin");
        }
        if (ret == -3)
        {
            return rsp_err(rsp, rsp_size, id, "readonly");
        }
        if (ret == -2)
        {
            return rsp_err(rsp, rsp_size, id, "not_ready");
        }
        snprintf(tmp, sizeof(tmp), "\"pin\":\"%s\",\"val\":%d", pin, val);
        return rsp_ok(rsp, rsp_size, id, tmp);
    }

    if (strcmp(cmd, "log.cdc") == 0)
    {
        if (json_get_int(line, "passthru_mute", &lvl) == 0)
        {
            ulog_cdc_set_passthru_mute(lvl ? 1 : 0);
        }
        snprintf(tmp, sizeof(tmp), "\"passthru_mute\":%d,\"held\":%d",
                 ulog_cdc_get_passthru_mute(), ulog_cdc_passthru_held());
        return rsp_ok(rsp, rsp_size, id, tmp);
    }

    if (strcmp(cmd, "log.lvl") == 0)
    {
        if (json_get_int(line, "lvl", &lvl) != 0)
        {
            return rsp_err(rsp, rsp_size, id, "no_lvl");
        }
#ifdef ULOG_USING_FILTER
        ulog_global_filter_lvl_set((rt_uint32_t)lvl);
        if (json_get_string(line, "tag", tag, sizeof(tag)) == 0)
        {
            ulog_tag_lvl_filter_set(tag, (rt_uint32_t)lvl);
            snprintf(tmp, sizeof(tmp), "\"lvl\":%d,\"tag\":\"%s\"", lvl, tag);
        }
        else
        {
            snprintf(tmp, sizeof(tmp), "\"lvl\":%d", lvl);
        }
        return rsp_ok(rsp, rsp_size, id, tmp);
#else
        (void)tag;
        return rsp_err(rsp, rsp_size, id, "filter_off");
#endif
    }

    if (strcmp(cmd, "cfg.get") == 0)
    {
        if (cfg_to_json(tmp, (int)sizeof(tmp)) < 0)
        {
            return rsp_err(rsp, rsp_size, id, "cfg_fail");
        }
        return rsp_ok(rsp, rsp_size, id, tmp);
    }

    if (strcmp(cmd, "cfg.set") == 0)
    {
        int pa = -1;
        int has = 0;
        const char *p;

        p = strstr(line, "\"recv_id\"");
        if (p != NULL)
        {
            unsigned long rid = 0;
            const char *q = strchr(p, ':');
            if (q != NULL)
            {
                rid = strtoul(q + 1, NULL, 10);
                if (rid != 0)
                {
                    cfg_set_recv_id((uint32_t)rid);
                    has = 1;
                }
            }
        }
        p = strstr(line, "\"device_id\"");
        if (p != NULL)
        {
            unsigned long did = 0;
            const char *q = strchr(p, ':');
            if (q != NULL)
            {
                did = strtoul(q + 1, NULL, 10);
                if (did != 0)
                {
                    cfg_set_device_id((uint32_t)did);
                    has = 1;
                }
            }
        }
        if (json_get_int(line, "pa_enable", &pa) == 0)
        {
            cfg_set_pa_enable(pa ? 1 : 0);
            has = 1;
        }
        {
            int off = -1;
            if (json_get_int(line, "charge_offset_mv", &off) == 0)
            {
                if (off < 0)
                {
                    off = 0;
                }
                cfg_set_charge_offset_mv((uint16_t)off);
                has = 1;
            }
        }
        if (json_get_string(line, "hw_ver", pin, sizeof(pin)) == 0)
        {
            cfg_set_hw_ver(pin);
            has = 1;
        }
        if (!has)
        {
            return rsp_err(rsp, rsp_size, id, "no_field");
        }
        if (cfg_to_json(tmp, (int)sizeof(tmp)) < 0)
        {
            return rsp_err(rsp, rsp_size, id, "cfg_fail");
        }
        return rsp_ok(rsp, rsp_size, id, tmp);
    }

    /* mode.get / test.* 调试自检 */
    ret = cli_test_handle(cmd, line, id, rsp, rsp_size);
    if (ret != 0)
    {
        return ret;
    }

    return rsp_err(rsp, rsp_size, id, "unknown_cmd");
}
