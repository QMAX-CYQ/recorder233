from pathlib import Path

root=Path(__file__).resolve().parent
text=(root.parent/'FATFS/App/fatfs.c').read_text(encoding='utf-8')
start=text.index('DWORD get_fattime(void)')
end=text.index('/* USER CODE BEGIN Application */',start)
function=text[start:end]
prelude=r'''
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
'''
tests=r'''
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
'''
(root/'fattime_test.c').write_text(prelude+function+tests,encoding='utf-8')
