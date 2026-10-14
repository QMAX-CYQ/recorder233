#ifndef __DEBUG_H
#define __DEBUG_H

#include "string.h"
#include "stdarg.h"  // 包含标准可变参数宏的头文件
#include "stdint.h"

enum 
{
    LOG_ERROR_LEVEL = 0,
    LOG_WARN_LEVEL,
    LOG_INFO_LEVEL,
    LOG_PRINTF_LEVEL,
    LOG_LEVEL_NUM
};

void sys_printf(uint8_t level, const char *tag, const char *fmt, ...);

#define DEBUG_ERROR(fmt,...)   sys_printf(LOG_ERROR_LEVEL,"ERROR: ", fmt, ##__VA_ARGS__);
#define DEBUG_WARN(fmt,...)    sys_printf(LOG_WARN_LEVEL,"WARN: ", fmt, ##__VA_ARGS__);
#define DEBUG_INFO(fmt,...)    sys_printf(LOG_INFO_LEVEL,"INFO: ", fmt, ##__VA_ARGS__);
#define DEBUG_PRINTF(fmt,...)   sys_printf(LOG_PRINTF_LEVEL,"PRINTF: ", fmt, ##__VA_ARGS__);
#define LEVEL_SET LOG_PRINTF_LEVEL




#endif

