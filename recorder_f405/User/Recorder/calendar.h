#ifndef __CALENDAR_H
#define __CALENDAR_H

#include "stdint.h"

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

#endif

