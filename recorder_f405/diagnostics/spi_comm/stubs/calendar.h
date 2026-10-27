#include <stdint.h>
#include <stdio.h>
/* 避免测试桩的日历结构与 Windows 标准库 time_t 冲突。 */
#define time_t test_calendar_time_t
typedef struct {
    uint16_t year;
    uint8_t month, day, hour, min, second;
} time_t;
uint8_t Calendar_ReadTime(time_t *value);
