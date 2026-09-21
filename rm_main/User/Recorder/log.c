#include "log.h"
#include "stdio.h"
#include "calendar.h"
#include "FreeRTOS.h"
#include "recorder_inter.h"
extern time_t time;
extern void RecorderData(const char *buf,size_t len);

void sys_printf(uint8_t level, const char *tag, const char *fmt, ...)
{
    if (level >= LOG_LEVEL_NUM || level >= LEVEL_SET || fmt == NULL)
        return;
    size_t len;
    char buf[256 + 32];
    int off = 0;
    const char *log_tag = (tag != NULL) ? tag : "";

    
    #if USE_RTC
    GetTime();
    off = snprintf(buf, sizeof(buf), "%02d%02d-%02d:%02d:%02d %s",
                   time.month, time.day, time.hour, time.min, time.second, log_tag);
    if (off < 0 || off >= (int)sizeof(buf))
        return;
	#else
		off = snprintf(buf, sizeof(buf), "%s",log_tag);
    if (off < 0 || off >= (int)sizeof(buf))
        return;
    #endif
    
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf + off, sizeof(buf) - off, fmt, args);
    va_end(args);
	len = strlen(buf);
    RecorderData(buf,len);
}




