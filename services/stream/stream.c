/**
 * @file stream.c
 * @brief stream 骨架：cli 已建成；gnss/rdss 随 USE_* 与 sink 填充
 */
#include "stream.h"
#include "stream_ports.h"
#include "product_config.h"
#include "mode.h"

#include <rtthread.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    const char *name;
    uint8_t ch;
    uint8_t built;
    uint8_t enable;
    stream_sink_fn sink;
} stream_item_t;

static stream_item_t s_streams[] = {
    {STREAM_NAME_CLI, STREAM_CH_CLI, 1, 1, NULL},
#if USE_GNSS
    {STREAM_NAME_GNSS, STREAM_CH_GNSS, 1, 0, NULL},
#else
    {STREAM_NAME_GNSS, STREAM_CH_GNSS, 0, 0, NULL},
#endif
#if USE_RDSS
    {STREAM_NAME_RDSS, STREAM_CH_RDSS, 1, 0, NULL},
#else
    {STREAM_NAME_RDSS, STREAM_CH_RDSS, 0, 0, NULL},
#endif
};

static stream_item_t *stream_find(const char *name)
{
    int i;

    if (name == NULL)
    {
        return NULL;
    }

    for (i = 0; i < (int)(sizeof(s_streams) / sizeof(s_streams[0])); i++)
    {
        if (strcmp(s_streams[i].name, name) == 0)
        {
            return &s_streams[i];
        }
    }
    return NULL;
}

int stream_init(void)
{
    return STREAM_OK;
}

#if USE_CLI
/** COMPONENT 级：stream 表；须早于 APP 级 cli */
static int app_stream_init(void)
{
    return stream_init();
}
INIT_COMPONENT_EXPORT(app_stream_init);
#endif

int stream_set_enable(const char *name, int enable)
{
    stream_item_t *item = stream_find(name);
    uint8_t flags;
    uint8_t bit = 0;

    if (item == NULL)
    {
        return STREAM_ERR_UNKNOWN;
    }
    if (!item->built)
    {
        return STREAM_ERR_NOT_BUILT;
    }

    if (strcmp(name, STREAM_NAME_GNSS) == 0)
    {
        bit = MODE_PT_GNSS;
    }
    else if (strcmp(name, STREAM_NAME_RDSS) == 0)
    {
        bit = MODE_PT_RDSS;
    }

    if (bit != 0)
    {
        flags = mode_passthru_flags_get();
        if (enable)
        {
            flags = (uint8_t)(flags | bit);
        }
        else
        {
            flags = (uint8_t)(flags & ~bit);
        }
        if (mode_passthru_set(flags) != RT_EOK)
        {
            return STREAM_ERR_DISABLED;
        }
        /* 表内 enable 与 MODE 异步对齐：先按请求置位，拒绝时 MODE 不会开通道 */
        item->enable = enable ? 1 : 0;
        return STREAM_OK;
    }

    item->enable = enable ? 1 : 0;
    return STREAM_OK;
}

int stream_is_enabled(const char *name)
{
    stream_item_t *item = stream_find(name);

    if (item == NULL)
    {
        return STREAM_ERR_UNKNOWN;
    }
    if (!item->built)
    {
        return STREAM_ERR_NOT_BUILT;
    }
    if (strcmp(name, STREAM_NAME_GNSS) == 0)
    {
        return (mode_passthru_flags_get() & MODE_PT_GNSS) ? 1 : 0;
    }
    if (strcmp(name, STREAM_NAME_RDSS) == 0)
    {
        return (mode_passthru_flags_get() & MODE_PT_RDSS) ? 1 : 0;
    }
    return item->enable ? 1 : 0;
}

int stream_is_built(const char *name)
{
    stream_item_t *item = stream_find(name);

    if (item == NULL)
    {
        return STREAM_ERR_UNKNOWN;
    }
    return item->built ? 1 : 0;
}

int stream_set_sink(const char *name, stream_sink_fn sink)
{
    stream_item_t *item = stream_find(name);

    if (item == NULL)
    {
        return STREAM_ERR_UNKNOWN;
    }
    if (!item->built)
    {
        return STREAM_ERR_NOT_BUILT;
    }

    item->sink = sink;
    return STREAM_OK;
}

int stream_write(const char *name, const uint8_t *data, uint32_t len)
{
    stream_item_t *item = stream_find(name);

    if ((item == NULL) || (data == NULL) || (len == 0))
    {
        return STREAM_ERR_PARAM;
    }
    if (!item->built)
    {
        return STREAM_ERR_NOT_BUILT;
    }
    if (stream_is_enabled(name) <= 0)
    {
        return STREAM_ERR_DISABLED;
    }
    if (item->sink == NULL)
    {
        return STREAM_ERR_NO_SINK;
    }

    return item->sink(data, len);
}

int stream_list_json(char *buf, int buflen)
{
    int i;
    int n = 0;
    int ret;

    if ((buf == NULL) || (buflen < 4))
    {
        return STREAM_ERR_PARAM;
    }

    ret = snprintf(buf + n, (size_t)(buflen - n), "[");
    if (ret < 0 || ret >= (buflen - n))
    {
        return STREAM_ERR_PARAM;
    }
    n += ret;

    for (i = 0; i < (int)(sizeof(s_streams) / sizeof(s_streams[0])); i++)
    {
        int en = s_streams[i].enable;
        int is_en;

        if (s_streams[i].built)
        {
            is_en = stream_is_enabled(s_streams[i].name);
            if (is_en >= 0)
            {
                en = is_en ? 1 : 0;
            }
        }
        ret = snprintf(buf + n, (size_t)(buflen - n),
                       "%s{\"name\":\"%s\",\"ch\":%u,\"built\":%u,\"enable\":%u}",
                       (i == 0) ? "" : ",",
                       s_streams[i].name,
                       (unsigned)s_streams[i].ch,
                       (unsigned)s_streams[i].built,
                       (unsigned)en);
        if (ret < 0 || ret >= (buflen - n))
        {
            return STREAM_ERR_PARAM;
        }
        n += ret;
    }

    ret = snprintf(buf + n, (size_t)(buflen - n), "]");
    if (ret < 0 || ret >= (buflen - n))
    {
        return STREAM_ERR_PARAM;
    }
    n += ret;
    return n;
}

const char *stream_err_str(int err)
{
    switch (err)
    {
    case STREAM_OK:
        return "ok";
    case STREAM_ERR_UNKNOWN:
        return "unknown";
    case STREAM_ERR_NOT_BUILT:
        return "not_built";
    case STREAM_ERR_DISABLED:
        return "disabled";
    case STREAM_ERR_NO_SINK:
        return "no_sink";
    case STREAM_ERR_PARAM:
        return "param";
    default:
        return "error";
    }
}
