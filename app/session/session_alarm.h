/**
 * @file session_alarm.h
 * @brief 会话调度：ALARM 2/5min、ON 10min、LOW_BATT 单次
 */
#ifndef __SESSION_ALARM_H__
#define __SESSION_ALARM_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * ALARM 会话：立刻定位发信 1 次，其后 0～24h 每 2min、24～48h 每 5min；
 * 满 48h 向 MODE 发 MODE_EVT_ALARM_EXPIRE。由 mode 进/出 ALARM 时调用。
 */
void session_alarm_start(void);
void session_alarm_stop(void);

/**
 * ON 会话：立刻 1 次 + 每 10min；报文模式 N。
 * 由 mode 进入 ON（开机确认/48h 到期/退出充电回 ON 等）时 start，离开 ON 时 stop。
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

#ifdef __cplusplus
}
#endif

#endif /* __SESSION_ALARM_H__ */
