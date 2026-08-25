/**
 * @file session_alarm.h
 * @brief 会话：ALARM 0～24h/2min、24～48h/5min、48h 起 10min（满 72h 仍停 ALARM）；ON 每 10min
 */
#ifndef __SESSION_ALARM_H__
#define __SESSION_ALARM_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * ALARM：立刻 1 拍，其后 0～24h / 2min、24～48h / 5min、48h 起 / 10min；
 * 报文 A。满 72h 不切 MODE，灯仍双闪。未校时锚点=0，先按 2min。
 */
void session_alarm_start(void);
/** 复位后续告警；start_unix=0 表示尚未锁墙钟起点 */
void session_alarm_resume(uint32_t start_unix);
void session_alarm_stop(void);

/**
 * ON：立刻 1 次 + 每 10min；报文 N。仅人开机等独立通道，不由告警到期进入。
 */
void session_on_start(void);
void session_on_stop(void);

/**
 * LOW_BATT：只跑 1 拍（模式 N），结束后 MODE_EVT_LOW_BATT_DONE。
 * 与 ON/ALARM 互斥（内部会停掉前序会话）。
 */
void session_lowbatt_once(void);

/** 停 ALARM/ON/LOW_BATT 任一当前会话，并关 GNSS/RDSS 电源路径 */
void session_stop_all(void);

/**
 * 调试：强制跑 1 拍（模式 N），不改 MODE、不发 LOW_BATT_DONE。
 * 会打断当前会话；结束后会话 kind=NONE（需 MODE 再 start 才恢复周期）。
 */
void session_test_once(void);

/** RTC 10s ISR 钩子：只投 WU 命令，线程里用 Unix 判断是否到点（不数 10s 当 72h） */
void session_on_rtc_wu(void);

/** 最近一拍是否因无卡空过（SHOT_IDLE 时 MODE 读取） */
int session_last_was_nosim(void);

#ifdef __cplusplus
}
#endif

#endif /* __SESSION_ALARM_H__ */
