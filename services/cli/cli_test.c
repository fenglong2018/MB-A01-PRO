/**
 * @file cli_test.c
 * @brief 板内自检 / 只读查询（经 USB JSON CLI）
 */
#include "cli_test.h"
#include "cli_json.h"
#include "config.h"
#include "mode.h"
#include "session_alarm.h"
#include "board_pins.h"

#include <rtthread.h>
#include <rtdevice.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if USE_ADC_BAT
#include "adc_bat.h"
#endif
#if USE_LED
#include "led.h"
#endif
#if USE_KEY
#include "key.h"
#endif
#if USE_GNSS
#include "gnss.h"
#endif
#if USE_RDSS
#include "rdss.h"
#endif
#if USE_RTC
#include "rtc.h"
#endif
#if USE_PM
#include "pm.h"
#endif

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
                    id, err ? err : "error");
}

static const char *json_find_key(const char *json, const char *key)
{
    char pattern[40];
    const char *p;

    if ((json == RT_NULL) || (key == RT_NULL))
    {
        return RT_NULL;
    }
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(json, pattern);
    if (p == RT_NULL)
    {
        return RT_NULL;
    }
    p = strchr(p + strlen(pattern), ':');
    return (p != RT_NULL) ? (p + 1) : RT_NULL;
}

static int json_get_int(const char *json, const char *key, int *out)
{
    const char *p = json_find_key(json, key);

    if ((p == RT_NULL) || (out == RT_NULL))
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

static const char *mode_name(mode_state_t st)
{
    switch (st)
    {
    case MODE_ST_OFF:       return "OFF";
    case MODE_ST_BATT:      return "BATT";
    case MODE_ST_ON:        return "ON";
    case MODE_ST_ALARM:     return "ALARM";
    case MODE_ST_CHARGE:    return "CHARGE";
    case MODE_ST_FORCE_OFF: return "FORCE_OFF";
    case MODE_ST_PASSTHRU:  return "PASSTHRU";
    case MODE_ST_LOW_BATT:  return "LOW_BATT";
    default:                return "?";
    }
}

static const char *lvl_name(int lv)
{
#if USE_ADC_BAT
    switch ((adc_bat_level_t)lv)
    {
    case ADC_BAT_LVL_OK:      return "OK";
    case ADC_BAT_LVL_WARN:    return "WARN";
    case ADC_BAT_LVL_PROTECT: return "PROTECT";
    default:                  return "?";
    }
#else
    (void)lv;
    return "NA";
#endif
}

static int pin_raw(int rt_pin)
{
    return rt_pin_read(rt_pin);
}

int cli_test_handle(const char *cmd, const char *line, int id, char *rsp, int rsp_size)
{
    char tmp[CLI_JSON_TMP_SIZE];

    if ((cmd == RT_NULL) || (rsp == RT_NULL))
    {
        return -1;
    }

    /* ---------- mode.get ---------- */
    if (strcmp(cmd, "mode.get") == 0)
    {
        mode_state_t st = mode_state_get();
        snprintf(tmp, sizeof(tmp),
                 "\"state\":%d,\"name\":\"%s\",\"pt\":%u",
                 (int)st, mode_name(st), (unsigned)mode_passthru_flags_get());
        return rsp_ok(rsp, rsp_size, id, tmp);
    }

    /* ---------- test.key ---------- */
    if (strcmp(cmd, "test.key") == 0)
    {
#if !USE_KEY
        return rsp_err(rsp, rsp_size, id, "not_built");
#else
        int sos = pin_raw(SOS_KEY_RT_PIN);
        int usb = pin_raw(USB_IN_RT_PIN);
        int fall = pin_raw(FALL_KEY_RT_PIN);
        int sim_pin = pin_raw(RD_BD_SIMCARD_RT_PIN);
        snprintf(tmp, sizeof(tmp),
                 "\"sos\":%d,\"usb\":%d,\"fall\":%d,\"sim_pin\":%d,\"sim_present\":%d",
                 sos, usb, fall, sim_pin, sim_present());
        return rsp_ok(rsp, rsp_size, id, tmp);
#endif
    }

    /* ---------- test.adc ---------- */
    if (strcmp(cmd, "test.adc") == 0)
    {
#if !USE_ADC_BAT
        return rsp_err(rsp, rsp_size, id, "not_built");
#else
        adc_bat_sample_t s;
        if (adc_bat_read(&s) != 0 || !s.valid)
        {
            return rsp_err(rsp, rsp_size, id, "not_ready");
        }
        snprintf(tmp, sizeof(tmp),
                 "\"pct\":%u,\"mv\":%u,\"vdda\":%u,\"level\":%d,\"level_name\":\"%s\"",
                 (unsigned)s.percent, (unsigned)s.vbat_mv, (unsigned)s.vdda_mv,
                 (int)s.level, lvl_name((int)s.level));
        return rsp_ok(rsp, rsp_size, id, tmp);
#endif
    }

    /* ---------- test.led ---------- */
    if (strcmp(cmd, "test.led") == 0)
    {
#if !USE_LED
        return rsp_err(rsp, rsp_size, id, "not_built");
#else
        int mode = -1;
        int pct = -1;
        (void)json_get_int(line, "mode", &mode);
        (void)json_get_int(line, "pct", &pct);
        if ((mode < 0) && (pct < 0))
        {
            return rsp_err(rsp, rsp_size, id, "no_field");
        }
        if (pct >= 0)
        {
            if (pct > 100)
            {
                pct = 100;
            }
            led_post_percent((uint8_t)pct);
        }
        if (mode >= 0)
        {
            led_post_mode((uint8_t)mode);
        }
        snprintf(tmp, sizeof(tmp), "\"mode\":%d,\"pct\":%d", mode, pct);
        return rsp_ok(rsp, rsp_size, id, tmp);
#endif
    }

    /* ---------- test.gnss.fix ---------- */
    if (strcmp(cmd, "test.gnss.fix") == 0)
    {
#if !USE_GNSS
        return rsp_err(rsp, rsp_size, id, "not_built");
#else
        gnss_msg_t msg;
        rt_err_t er;
        int wait_ms = GNSS_FIX_TIMEOUT_MS + 5000;

        (void)json_get_int(line, "wait_ms", &wait_ms);
        if (wait_ms < 1000)
        {
            wait_ms = 1000;
        }
        if (gnss_passthru_active())
        {
            return rsp_err(rsp, rsp_size, id, "busy_passthru");
        }
        if (gnss_start_fix() != RT_EOK)
        {
            return rsp_err(rsp, rsp_size, id, "start_fail");
        }
        er = rt_mq_recv(gnss_result_mq(), &msg, sizeof(msg),
                        rt_tick_from_millisecond((rt_int32_t)wait_ms));
        if (er != RT_EOK)
        {
            return rsp_err(rsp, rsp_size, id, "timeout");
        }
        snprintf(tmp, sizeof(tmp),
                 "\"fix_ok\":%u,\"reason\":%u,\"valid\":%u,\"lat_e7\":%ld,\"lon_e7\":%ld,"
                 "\"alt_dm\":%d,\"sats\":%u,\"q\":%u,\"utc\":\"%.10s\"",
                 (unsigned)msg.ok, (unsigned)msg.reason, (unsigned)msg.fix.valid,
                 (long)msg.fix.lat_e7, (long)msg.fix.lon_e7,
                 (int)msg.fix.alt_dm, (unsigned)msg.fix.satellites,
                 (unsigned)msg.fix.quality, msg.fix.utc);
        return rsp_ok(rsp, rsp_size, id, tmp);
#endif
    }

    /* ---------- test.rdss.card ---------- */
    if (strcmp(cmd, "test.rdss.card") == 0)
    {
#if !USE_RDSS
        return rsp_err(rsp, rsp_size, id, "not_built");
#else
        int wait_ms = (int)RDSS_CARD_TIMEOUT_MS;
        (void)json_get_int(line, "wait_ms", &wait_ms);
        if (rdss_passthru_active())
        {
            return rsp_err(rsp, rsp_size, id, "busy_passthru");
        }
        if (rdss_ensure_card((uint32_t)wait_ms) != RT_EOK)
        {
            return snprintf(rsp, (size_t)rsp_size,
                            "{\"type\":\"rsp\",\"id\":%d,\"ok\":0,\"err\":\"card_fail\","
                            "\"card_id\":%lu}\r\n",
                            id, (unsigned long)rdss_get_card_id());
        }
        snprintf(tmp, sizeof(tmp), "\"card_id\":%lu",
                 (unsigned long)rdss_get_card_id());
        return rsp_ok(rsp, rsp_size, id, tmp);
#endif
    }

    /* ---------- test.rdss.send ---------- */
    if (strcmp(cmd, "test.rdss.send") == 0)
    {
#if !USE_RDSS
        return rsp_err(rsp, rsp_size, id, "not_built");
#else
        static const uint8_t k_payload[] = "TEST";
        rdss_msg_t msg;
        int wait_ms = (int)(RDSS_BEAM_TIMEOUT_MS + RDSS_FKI_TIMEOUT_MS + 5000);

        (void)json_get_int(line, "wait_ms", &wait_ms);
        if (rdss_passthru_active())
        {
            return rsp_err(rsp, rsp_size, id, "busy_passthru");
        }
        if (rdss_start_send(k_payload, (uint16_t)(sizeof(k_payload) - 1u)) != RT_EOK)
        {
            return rsp_err(rsp, rsp_size, id, "start_fail");
        }
        if (rt_mq_recv(rdss_result_mq(), &msg, sizeof(msg),
                       rt_tick_from_millisecond((rt_int32_t)wait_ms)) != RT_EOK)
        {
            return rsp_err(rsp, rsp_size, id, "timeout");
        }
        snprintf(tmp, sizeof(tmp), "\"tx_ok\":%u,\"reason\":%u",
                 (unsigned)msg.ok, (unsigned)msg.reason);
        return rsp_ok(rsp, rsp_size, id, tmp);
#endif
    }

    /* ---------- test.session.once ---------- */
    if (strcmp(cmd, "test.session.once") == 0)
    {
        session_test_once();
        return rsp_ok(rsp, rsp_size, id, "\"queued\":1");
    }

    /* ---------- test.rtc ---------- */
    if (strcmp(cmd, "test.rtc") == 0)
    {
#if !USE_RTC
        return rsp_err(rsp, rsp_size, id, "not_built");
#else
        unsigned long set_u = 0;
        const char *p = strstr(line, "\"unix\"");
        if (p != RT_NULL)
        {
            const char *q = strchr(p, ':');
            if (q != RT_NULL)
            {
                set_u = strtoul(q + 1, RT_NULL, 10);
                if (set_u != 0)
                {
                    if (rtc_post_unix((uint32_t)set_u, RTC_SRC_CLI) != RT_EOK)
                    {
                        return rsp_err(rsp, rsp_size, id, "post_fail");
                    }
                    rt_thread_mdelay(20);
                }
            }
        }
        snprintf(tmp, sizeof(tmp), "\"unix\":%lu,\"synced\":%d",
                 (unsigned long)rtc_get_unix(), rtc_is_synced());
        return rsp_ok(rsp, rsp_size, id, tmp);
#endif
    }

    /* ---------- test.pm ---------- */
    if (strcmp(cmd, "test.pm") == 0)
    {
#if !USE_PM
        return rsp_err(rsp, rsp_size, id, "not_built");
#else
        int lock = -1;
        int unlock = -1;
        int hold_ms = 0;
        (void)json_get_int(line, "lock", &lock);
        (void)json_get_int(line, "unlock", &unlock);
        (void)json_get_int(line, "hold_ms", &hold_ms);
        if (lock > 0)
        {
            pm_lock();
        }
        if (hold_ms > 0)
        {
            if (lock <= 0)
            {
                pm_lock();
            }
            rt_thread_mdelay((rt_int32_t)hold_ms);
            if (lock <= 0)
            {
                pm_unlock();
            }
        }
        if (unlock > 0)
        {
            pm_unlock();
        }
        snprintf(tmp, sizeof(tmp), "\"lock_count\":%lu",
                 (unsigned long)pm_lock_count());
        return rsp_ok(rsp, rsp_size, id, tmp);
#endif
    }

    /* ---------- test.list ---------- */
    if (strcmp(cmd, "test.list") == 0)
    {
        return rsp_ok(rsp, rsp_size, id,
                      "\"cmds\":[\"mode.get\",\"test.key\",\"test.adc\",\"test.led\","
                      "\"test.gnss.fix\",\"test.rdss.card\",\"test.rdss.send\","
                      "\"test.session.once\",\"test.rtc\",\"test.pm\",\"test.list\"]");
    }

    (void)line;
    return 0; /* 非本模块命令 */
}
