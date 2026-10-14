#include "calendar.h"
#include "stdio.h"
#include "recorder_inter.h"
#include "rtc.h"
volatile rtc_read_diag_t g_rtc_read_diag;
/* 保留旧接口的全局日历；实际格式化调用使用局部快照。 */
time_t time;

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

uint8_t Calendar_ReadTime(time_t *value)
{
    RTC_TimeTypeDef rtc_time;
    RTC_DateTypeDef rtc_date;
    if (value == NULL) return 0U;
    if (!RTC_ReadSnapshot(&rtc_time, &rtc_date)) {
        uint32_t irq_state = __get_PRIMASK();
        const time_t fallback = {0U, 1U, 1U, 0U, 0U, 0U};
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
    time_t snapshot;
    uint32_t irq_state;
    (void)Calendar_ReadTime(&snapshot);
    irq_state = __get_PRIMASK();
    __disable_irq();
    time = snapshot;
    __set_PRIMASK(irq_state);
}

void GetTimeToFileName(char *filename)
{
    time_t snapshot;
    if (filename == NULL) return;
    (void)Calendar_ReadTime(&snapshot);
    snprintf(filename, 13, "%02u%02u%02u%02u%02u%02u",
             (unsigned int)snapshot.year, (unsigned int)snapshot.month,
             (unsigned int)snapshot.day, (unsigned int)snapshot.hour,
             (unsigned int)snapshot.min, (unsigned int)snapshot.second);
}
