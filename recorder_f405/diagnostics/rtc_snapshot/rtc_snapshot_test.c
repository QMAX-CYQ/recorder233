
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint32_t DWORD;
typedef struct { uint8_t Hours,Minutes,Seconds; } RTC_TimeTypeDef;
typedef struct { uint8_t Year,Month,Date; } RTC_DateTypeDef;
typedef struct { void *Instance; } RTC_HandleTypeDef;
typedef int HAL_StatusTypeDef;
#define HAL_OK 0
#define HAL_ERROR 1
#define RTC_FORMAT_BIN 0
#define USE_RTC 1
static RTC_HandleTypeDef hrtc;
static RTC_TimeTypeDef mock_time;
static RTC_DateTypeDef mock_date;
static unsigned irq_mask,phase;
static int mock_time_status,mock_date_status;
static uint32_t __get_PRIMASK(void) { return irq_mask; }
static void __disable_irq(void) { irq_mask=1; }
static void __set_PRIMASK(uint32_t value) { irq_mask=value; }
static HAL_StatusTypeDef HAL_RTC_GetTime(RTC_HandleTypeDef *h,RTC_TimeTypeDef *t,int format) {
    assert(h==&hrtc && irq_mask==1 && phase==0 && format==RTC_FORMAT_BIN);
    phase=1;*t=mock_time;return mock_time_status;
}
static HAL_StatusTypeDef HAL_RTC_GetDate(RTC_HandleTypeDef *h,RTC_DateTypeDef *d,int format) {
    assert(h==&hrtc && irq_mask==1 && phase==1 && format==RTC_FORMAT_BIN);
    phase=2;*d=mock_date;return mock_date_status;
}
#ifndef __CALENDAR_H
#define __CALENDAR_H


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

}calendar_time_t;

/* 年份保持原有的 2000 年偏移语义；失败时输出 2000-01-01。 */
uint8_t Calendar_ReadTime(calendar_time_t *value);

#endif

volatile rtc_read_diag_t g_rtc_read_diag;
/* 保留旧接口的全局日历；实际格式化调用使用局部快照。 */
calendar_time_t time;

static void rtc_record_result(rtc_snapshot_result_t result)
{
    uint32_t irq_state = __get_PRIMASK();
    __disable_irq();
    g_rtc_read_diag.snapshot_calls++;
    g_rtc_read_diag.last_result = (uint32_t)result;
    switch (result) {
    case RTC_SNAPSHOT_OK: g_rtc_read_diag.snapshot_ok++; break;
    case RTC_SNAPSHOT_NOT_READY: g_rtc_read_diag.rtc_uninitialized++; break;
    case RTC_SNAPSHOT_READ_ERROR: g_rtc_read_diag.read_error++; break;
    case RTC_SNAPSHOT_INVALID_DATE: g_rtc_read_diag.invalid_date++; break;
    case RTC_SNAPSHOT_INVALID_TIME: g_rtc_read_diag.invalid_time++; break;
    case RTC_SNAPSHOT_INVALID_ARGUMENT: g_rtc_read_diag.invalid_argument++; break;
    case RTC_SNAPSHOT_DISABLED: g_rtc_read_diag.rtc_disabled++; break;
    default: break;
    }
    __set_PRIMASK(irq_state);
}

uint8_t RTC_ReadSnapshot(RTC_TimeTypeDef *rtc_time, RTC_DateTypeDef *rtc_date)
{
    if (rtc_time == NULL || rtc_date == NULL) {
        rtc_record_result(RTC_SNAPSHOT_INVALID_ARGUMENT);
        return 0U;
    }
#if USE_RTC
    RTC_TimeTypeDef local_time;
    RTC_DateTypeDef local_date;
    HAL_StatusTypeDef time_status, date_status;
    uint32_t irq_state;
    uint16_t year;
    uint8_t max_day;
    static const uint8_t month_days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (hrtc.Instance == NULL) {
        rtc_record_result(RTC_SNAPSHOT_NOT_READY);
        return 0U;
    }
    /* 所有取时入口共用短临界区，先读时间再读日期。
     * 即使读时间出错，也读取日期以释放影子寄存器锁。 */
    irq_state = __get_PRIMASK();
    __disable_irq();
    time_status = HAL_RTC_GetTime(&hrtc, &local_time, RTC_FORMAT_BIN);
    date_status = HAL_RTC_GetDate(&hrtc, &local_date, RTC_FORMAT_BIN);
    __set_PRIMASK(irq_state);
    if (time_status != HAL_OK || date_status != HAL_OK) {
        rtc_record_result(RTC_SNAPSHOT_READ_ERROR);
        return 0U;
    }
    if (local_date.Year > 99U || local_date.Month < 1U || local_date.Month > 12U) {
        rtc_record_result(RTC_SNAPSHOT_INVALID_DATE);
        return 0U;
    }
    year = (uint16_t)(2000U + local_date.Year);
    max_day = month_days[local_date.Month - 1U];
    /* 公历闰年规则，2000 年满足整除 400 的条件。 */
    if (local_date.Month == 2U && year % 4U == 0U &&
        (year % 100U != 0U || year % 400U == 0U)) max_day = 29U;
    if (local_date.Date < 1U || local_date.Date > max_day) {
        rtc_record_result(RTC_SNAPSHOT_INVALID_DATE);
        return 0U;
    }
    if (local_time.Hours > 23U || local_time.Minutes > 59U || local_time.Seconds > 59U) {
        rtc_record_result(RTC_SNAPSHOT_INVALID_TIME);
        return 0U;
    }
    *rtc_time = local_time;
    *rtc_date = local_date;
    rtc_record_result(RTC_SNAPSHOT_OK);
    return 1U;
#else
    rtc_record_result(RTC_SNAPSHOT_DISABLED);
    return 0U;
#endif
}

void RTC_RecordFatTimeFallback(void)
{
    uint32_t irq_state = __get_PRIMASK();
    __disable_irq();
    g_rtc_read_diag.fattime_fallback_count++;
    __set_PRIMASK(irq_state);
}

uint8_t Calendar_ReadTime(calendar_time_t *value)
{
    RTC_TimeTypeDef rtc_time;
    RTC_DateTypeDef rtc_date;
    if (value == NULL) return 0U;
    if (!RTC_ReadSnapshot(&rtc_time, &rtc_date)) {
        uint32_t irq_state = __get_PRIMASK();
        const calendar_time_t fallback = {0U, 1U, 1U, 0U, 0U, 0U};
        *value = fallback;
        __disable_irq();
        g_rtc_read_diag.calendar_fallback_count++;
        __set_PRIMASK(irq_state);
        return 0U;
    }
    value->year = rtc_date.Year;
    value->month = rtc_date.Month;
    value->day = rtc_date.Date;
    value->hour = rtc_time.Hours;
    value->min = rtc_time.Minutes;
    value->second = rtc_time.Seconds;
    return 1U;
}

void GetTime(void)
{
    calendar_time_t snapshot;
    uint32_t irq_state;
    (void)Calendar_ReadTime(&snapshot);
    irq_state = __get_PRIMASK();
    __disable_irq();
    time = snapshot;
    __set_PRIMASK(irq_state);
}

void GetTimeToFileName(char *filename)
{
    calendar_time_t snapshot;
    if (filename == NULL) return;
    (void)Calendar_ReadTime(&snapshot);
    snprintf(filename, 13, "%02u%02u%02u%02u%02u%02u",
             (unsigned int)snapshot.year, (unsigned int)snapshot.month,
             (unsigned int)snapshot.day, (unsigned int)snapshot.hour,
             (unsigned int)snapshot.min, (unsigned int)snapshot.second);
}
DWORD get_fattime(void)
{
  /* USER CODE BEGIN get_fattime */
  RTC_TimeTypeDef rtc_time;
  RTC_DateTypeDef rtc_date;
  /* RTC 不可用时返回有效的固定日期：2000-01-01 00:00:00。 */
  const DWORD fallback_time = ((DWORD)20U << 25) | ((DWORD)1U << 21) | ((DWORD)1U << 16);

  /* 与日志取时共用完整日历校验和并发保护。 */
  if (!RTC_ReadSnapshot(&rtc_time, &rtc_date))
  {
    RTC_RecordFatTimeFallback();
    return fallback_time;
  }

  /* RTC 年份以 2000 为基准，FAT 年份以 1980 为基准；秒字段精度为 2 秒。 */
  return ((DWORD)(rtc_date.Year + 20U) << 25) |
         ((DWORD)rtc_date.Month << 21) |
         ((DWORD)rtc_date.Date << 16) |
         ((DWORD)rtc_time.Hours << 11) |
         ((DWORD)rtc_time.Minutes << 5) |
         ((DWORD)rtc_time.Seconds >> 1);
  /* USER CODE END get_fattime */
}


static uint8_t check_date(unsigned year,unsigned month,unsigned day) {
    RTC_TimeTypeDef t={77,77,77};RTC_DateTypeDef d={77,77,77};uint8_t result;
    mock_date.Year=(uint8_t)year;mock_date.Month=(uint8_t)month;mock_date.Date=(uint8_t)day;
    phase=0;result=RTC_ReadSnapshot(&t,&d);
    assert(phase==2);
    if(!result) assert(t.Hours==77 && d.Month==77);
    return result;
}
int main(void) {
    calendar_time_t first,second;
    RTC_TimeTypeDef out_time;RTC_DateTypeDef out_date;
    char name[13];uint32_t count;
    hrtc.Instance=&hrtc;
    mock_time.Hours=22;mock_time.Minutes=49;mock_time.Seconds=21;
    assert(check_date(26,10,14));phase=0;assert(get_fattime()==0x5D4EB62AUL);
    assert(irq_mask==0);
    puts("PASS: actual shared RTC snapshot and FAT packing with 2-second precision");
    assert(check_date(0,2,29));assert(check_date(24,2,29));assert(check_date(96,2,29));
    assert(!check_date(25,2,29));assert(!check_date(26,2,31));
    assert(!check_date(26,4,31));assert(check_date(26,4,30));
    assert(!check_date(26,0,1));assert(!check_date(26,13,1));
    assert(!check_date(26,1,0));assert(!check_date(100,1,1));
    assert(check_date(99,12,31));
    puts("PASS: leap years, month lengths, valid and invalid date boundaries");
    mock_time.Hours=24;assert(!check_date(26,10,14));
    mock_time.Hours=23;mock_time.Minutes=60;assert(!check_date(26,10,14));
    mock_time.Minutes=59;mock_time.Seconds=60;assert(!check_date(26,10,14));
    mock_time.Seconds=59;
    phase=0;count=g_rtc_read_diag.fattime_fallback_count;
    mock_date.Month=2;mock_date.Date=31;
    assert(get_fattime()==0x28210000UL && g_rtc_read_diag.fattime_fallback_count==count+1);
    phase=0;count=g_rtc_read_diag.calendar_fallback_count;
    assert(!Calendar_ReadTime(&first) && first.year==0 && first.month==1 && first.day==1);
    assert(g_rtc_read_diag.calendar_fallback_count==count+1);
    puts("PASS: invalid time and separate FAT/calendar fallback counters");
    mock_date.Year=26;mock_date.Month=10;mock_date.Date=14;
    phase=0;mock_time_status=HAL_ERROR;irq_mask=1;
    assert(!RTC_ReadSnapshot(&out_time,&out_date) && phase==2 && irq_mask==1);
    mock_time_status=HAL_OK;mock_date_status=HAL_ERROR;phase=0;
    assert(!RTC_ReadSnapshot(&out_time,&out_date) && phase==2 && irq_mask==1);
    mock_date_status=HAL_OK;irq_mask=0;hrtc.Instance=NULL;
    phase=0;assert(!RTC_ReadSnapshot(&out_time,&out_date) && phase==0);
    assert(!RTC_ReadSnapshot(NULL,&out_date));hrtc.Instance=&hrtc;
    puts("PASS: RTC failures, always-read-date, IRQ restoration, null arguments");
    mock_date.Date=14;mock_time.Hours=23;mock_time.Minutes=59;mock_time.Seconds=59;
    phase=0;assert(Calendar_ReadTime(&first));
    mock_date.Date=15;mock_time.Hours=0;mock_time.Minutes=0;mock_time.Seconds=0;
    phase=0;assert(Calendar_ReadTime(&second));
    assert(first.day==14 && first.hour==23 && second.day==15 && second.hour==0);
    phase=0;GetTime();assert(time.day==15);
    phase=0;GetTimeToFileName(name);assert(strcmp(name,"261015000000")==0);
    assert(g_rtc_read_diag.snapshot_calls==g_rtc_read_diag.snapshot_ok+
        g_rtc_read_diag.rtc_uninitialized+g_rtc_read_diag.read_error+g_rtc_read_diag.invalid_date+
        g_rtc_read_diag.invalid_time+g_rtc_read_diag.invalid_argument+g_rtc_read_diag.rtc_disabled);
    puts("PASS: independent snapshots across midnight, legacy wrappers, diagnostics balance");
    return 0;
}
