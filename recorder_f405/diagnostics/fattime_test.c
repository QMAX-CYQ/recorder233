
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t DWORD;
typedef struct { uint8_t Hours,Minutes,Seconds; } RTC_TimeTypeDef;
typedef struct { uint8_t Year,Month,Date; } RTC_DateTypeDef;
typedef struct { void *Instance; } RTC_HandleTypeDef;
typedef int HAL_StatusTypeDef;
#define HAL_OK 0
#define HAL_ERROR 1
#define RTC_FORMAT_BIN 0
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
DWORD get_fattime(void)
{
  /* USER CODE BEGIN get_fattime */
  RTC_TimeTypeDef rtc_time;
  RTC_DateTypeDef rtc_date;
  HAL_StatusTypeDef time_status;
  HAL_StatusTypeDef date_status;
  uint32_t irq_state;
  /* RTC 不可用时返回有效的固定日期：2000-01-01 00:00:00。 */
  const DWORD fallback_time = ((DWORD)20U << 25) | ((DWORD)1U << 21) | ((DWORD)1U << 16);

  if (hrtc.Instance == NULL)
    return fallback_time;

  /* 必须先读时间再读日期，以释放 RTC 影子寄存器锁。
   * 短临界区避免其他任务在两次读取之间访问 RTC；不使用共享日历变量。 */
  irq_state = __get_PRIMASK();
  __disable_irq();
  time_status = HAL_RTC_GetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN);
  date_status = HAL_RTC_GetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN);
  __set_PRIMASK(irq_state);

  if ((time_status != HAL_OK) || (date_status != HAL_OK) ||
      (rtc_date.Year > 99U) || (rtc_date.Month < 1U) || (rtc_date.Month > 12U) ||
      (rtc_date.Date < 1U) || (rtc_date.Date > 31U) ||
      (rtc_time.Hours > 23U) || (rtc_time.Minutes > 59U) || (rtc_time.Seconds > 59U))
    return fallback_time;

  /* RTC 年份以 2000 为基准，FAT 年份以 1980 为基准；秒字段精度为 2 秒。 */
  return ((DWORD)(rtc_date.Year + 20U) << 25) |
         ((DWORD)rtc_date.Month << 21) |
         ((DWORD)rtc_date.Date << 16) |
         ((DWORD)rtc_time.Hours << 11) |
         ((DWORD)rtc_time.Minutes << 5) |
         ((DWORD)rtc_time.Seconds >> 1);
  /* USER CODE END get_fattime */
}


int main(void) {
    DWORD value;
    hrtc.Instance=&hrtc;
    mock_date.Year=26;mock_date.Month=10;mock_date.Date=14;
    mock_time.Hours=22;mock_time.Minutes=49;mock_time.Seconds=21;
    value=get_fattime();assert(value==0x5D4EB62AUL);
    assert(phase==2 && irq_mask==0);
    puts("PASS: 2026-10-14 22:49:21 encoded as FAT 22:49:20");
    phase=0;irq_mask=1;mock_date.Year=99;
    mock_date.Month=12;mock_date.Date=31;mock_time.Hours=23;mock_time.Minutes=59;mock_time.Seconds=59;
    value=get_fattime();assert((value>>25)+1980==2099 && (value&31)*2==58 && irq_mask==1);
    puts("PASS: year boundary, 2-second precision, previous IRQ mask restored");
    phase=0;irq_mask=0;mock_time_status=HAL_ERROR;
    assert(get_fattime()==0x28210000UL && phase==2 && irq_mask==0);
    puts("PASS: RTC read failure falls back and still reads date to unlock shadow registers");
    mock_time_status=HAL_OK;phase=0;mock_date.Month=0;
    assert(get_fattime()==0x28210000UL);
    phase=0;hrtc.Instance=NULL;
    assert(get_fattime()==0x28210000UL && phase==0);
    puts("PASS: invalid fields and uninitialized RTC use valid 2000-01-01 fallback");
    return 0;
}
