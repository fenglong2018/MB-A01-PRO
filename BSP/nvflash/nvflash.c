/**
 * @file nvflash.c
 * @brief Flash 双槽：magic+ver+seq+payload+CRC32；坏槽整段不用
 */
#include "nvflash.h"
#include "n32wb452_flash.h"
#include "n32wb452_iwdg.h"
#include "product_config.h"
#if USE_PM
#include "pm.h"
#endif

#include <string.h>

#define NV_MAGIC        0x4E564631u /* NVF1 */
#define NV_VER          1u

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint32_t ver;
    uint32_t seq;
    uint32_t len;
    uint8_t  payload[NVFLASH_PAYLOAD_MAX];
    uint32_t crc;
} nvflash_rec_t;

static uint32_t crc32_calc(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    uint32_t i;
    uint8_t b;
    uint8_t k;

    for (i = 0; i < n; i++)
    {
        c ^= p[i];
        for (k = 0; k < 8u; k++)
        {
            b = (uint8_t)(c & 1u);
            c >>= 1;
            if (b)
            {
                c ^= 0xEDB88320u;
            }
        }
    }
    return c ^ 0xFFFFFFFFu;
}

static uint32_t rec_crc(const nvflash_rec_t *r)
{
    return crc32_calc((const uint8_t *)r,
                      (uint32_t)(sizeof(*r) - sizeof(r->crc)));
}

static int rec_ok(const nvflash_rec_t *r)
{
    if ((r->magic != NV_MAGIC) || (r->ver != NV_VER))
    {
        return 0;
    }
    if (r->len > NVFLASH_PAYLOAD_MAX)
    {
        return 0;
    }
    return (rec_crc(r) == r->crc) ? 1 : 0;
}

static const nvflash_rec_t *slot_ptr(uint32_t addr)
{
    return (const nvflash_rec_t *)addr;
}

static int program_words(uint32_t addr, const uint32_t *w, uint32_t n)
{
    uint32_t i;
    FLASH_STS st;

    for (i = 0; i < n; i++)
    {
        IWDG_ReloadKey();
        st = FLASH_ProgramWord(addr + i * 4u, w[i]);
        if (st != FLASH_COMPL)
        {
            return -1;
        }
    }
    return 0;
}

int nvflash_init(void)
{
    return 0;
}

int nvflash_load(void *out, uint16_t out_max)
{
    const nvflash_rec_t *a;
    const nvflash_rec_t *b;
    const nvflash_rec_t *best;
    int a_ok;
    int b_ok;

    if ((out == 0) || (out_max == 0))
    {
        return -1;
    }

    a = slot_ptr(NVFLASH_SLOT_A);
    b = slot_ptr(NVFLASH_SLOT_B);
    a_ok = rec_ok(a);
    b_ok = rec_ok(b);
    best = 0;

    if (a_ok && b_ok)
    {
        best = (a->seq >= b->seq) ? a : b;
    }
    else if (a_ok)
    {
        best = a;
    }
    else if (b_ok)
    {
        best = b;
    }
    else
    {
        return -1;
    }

    if (best->len > out_max)
    {
        return -1;
    }
    memcpy(out, best->payload, best->len);
    return (int)best->len;
}

int nvflash_save(const void *payload, uint16_t len)
{
    const nvflash_rec_t *a;
    const nvflash_rec_t *b;
    nvflash_rec_t rec;
    uint32_t seq;
    uint32_t dest;
    uint32_t words;
    int a_ok;
    int b_ok;

    if ((payload == 0) || (len == 0) || (len > NVFLASH_PAYLOAD_MAX))
    {
        return -1;
    }

    a = slot_ptr(NVFLASH_SLOT_A);
    b = slot_ptr(NVFLASH_SLOT_B);
    a_ok = rec_ok(a);
    b_ok = rec_ok(b);

    if (a_ok && (a->len == len) && (memcmp(a->payload, payload, len) == 0))
    {
        return 0;
    }
    if (b_ok && (b->len == len) && (memcmp(b->payload, payload, len) == 0))
    {
        return 0;
    }

    seq = 1u;
    if (a_ok && (a->seq >= seq))
    {
        seq = a->seq + 1u;
    }
    if (b_ok && (b->seq >= seq))
    {
        seq = b->seq + 1u;
    }

    /* 写到 seq 较小或无效的槽 */
    if (a_ok && b_ok)
    {
        dest = (a->seq <= b->seq) ? NVFLASH_SLOT_A : NVFLASH_SLOT_B;
    }
    else if (a_ok)
    {
        dest = NVFLASH_SLOT_B;
    }
    else
    {
        dest = NVFLASH_SLOT_A;
    }

    memset(&rec, 0xFF, sizeof(rec));
    rec.magic = NV_MAGIC;
    rec.ver = NV_VER;
    rec.seq = seq;
    rec.len = len;
    memcpy(rec.payload, payload, len);
    rec.crc = rec_crc(&rec);

#if USE_PM
    pm_lock();
#endif
    FLASH_Unlock();
    IWDG_ReloadKey();
    if (FLASH_EraseOnePage(dest) != FLASH_COMPL)
    {
        FLASH_Lock();
#if USE_PM
        pm_unlock();
#endif
        return -1;
    }
    words = (uint32_t)((sizeof(rec) + 3u) / 4u);
    if (program_words(dest, (const uint32_t *)&rec, words) != 0)
    {
        FLASH_Lock();
#if USE_PM
        pm_unlock();
#endif
        return -1;
    }
    FLASH_Lock();
#if USE_PM
    pm_unlock();
#endif
    return 0;
}
