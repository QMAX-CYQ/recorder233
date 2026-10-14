#ifndef __CALENDAR_H
#define __CALENDAR_H

#include "stdint.h"
#include "rtc.h"

typedef enum {
  RTC_SNAPSHOT_OK = 0, RTC_SNAPSHOT_NOT_READY, RTC_SNAPSHOT_READ_ERROR,
  RTC_SNAPSHOT_INVALID_DATE, RTC_SNAPSHOT_INVALID_TIME,
  RTC_SNAPSHOT_INVALID_ARGUMENT, RTC_SNAPSHOT_DISABLED
} rtc_snapshot_result_t;
/* 只保存在 RAM 中，供 Watch 观察，不产生额外日志。 */
typedef struct {
  uint32_t snapshot_calls, snapshot_ok;
  uint32_t rtc_uninitialized, read_error, invalid_date, invalid_time;
  uint32_t invalid_argument, rtc_disabled, last_result;
  uint32_t fattime_fallback_count, calendar_fallback_count;
} rtc_read_diag_t;
extern volatile rtc_read_diag_t g_rtc_read_diag;
/* 成功返回 1；失败返回 0，保持调用者的输出结构不变。 */
uint8_t RTC_ReadSnapshot(RTC_TimeTypeDef *rtc_time, RTC_DateTypeDef *rtc_date);
void RTC_RecordFatTimeFallback(void);

void GetTimeToFileName(char *filename);
void GetTime(void);
typedef struct
{
  uint16_t year;
  uint8_t  month;
  uint8_t  day;
  uint8_t  hour;
  uint8_t  min;
  uint8_t  second;

}time_t;

/* 年份保持原有的 2000 年偏移语义；失败时输出 2000-01-01。 */
uint8_t Calendar_ReadTime(time_t *value);

#endif

