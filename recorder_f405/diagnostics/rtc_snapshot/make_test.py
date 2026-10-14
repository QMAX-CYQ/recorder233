from pathlib import Path
import re

root=Path(__file__).resolve().parent
project=root.parents[1]
def strip(text):
    text=re.sub(r'^#include[^\n]*\n','',text,flags=re.M)
    return re.sub(r'\btime_t\b','calendar_time_t',text)
header=strip((project/'User/Recorder/calendar.h').read_text(encoding='utf-8'))
calendar=strip((project/'User/Recorder/calendar.c').read_text(encoding='utf-8'))
fat=(project/'FATFS/App/fatfs.c').read_text(encoding='utf-8')
fat=fat[fat.index('DWORD get_fattime(void)'):fat.index('/* USER CODE BEGIN Application */')]
prelude=r'''
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
'''
tests=r'''
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
'''
(root/'rtc_snapshot_test.c').write_text(prelude+header+calendar+fat+tests,encoding='utf-8')
