#include "calendar.h"
#include "rtc.h"
#include "stdio.h"
#include "recorder_inter.h"
#if USE_RTC
RTC_TimeTypeDef sTime;
RTC_DateTypeDef sDate;
time_t time;

void GetTime(void)
{
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
    time.year   =  sDate.Year;
	  time.month  =  sDate.Month;
	  time.day    =  sDate.Date; 
	  time.hour   =  sTime.Hours; 
	  time.min    =  sTime.Minutes;
	  time.second =  sTime.Seconds;
 
}

void GetTimeToFileName(char *filename)
{
    GetTime();
	  uint16_t year = sDate.Year;
    uint8_t month = sDate.Month;
    uint8_t day   = sDate.Date;  
    uint8_t min   = sTime.Minutes;  
    uint8_t hour  = sTime.Hours; 
    uint8_t sec   = sTime.Seconds;
    

    if(filename != NULL)
    {
        snprintf(filename,13,"%02d%02d%02d%02d%02d%02d",year,month,day,hour,min,sec);
    }



}
#endif





