#include "log.h"
#include "stdio.h"
#include "FreeRTOS.h"
#include "usbd_cdc_if.h"
#include "usb_device.h"
#include "cmsis_os.h"

void sys_printf(uint8_t level, const char *tag, const char *fmt, ...)
{
    if (level >= LOG_LEVEL_NUM || level >= LEVEL_SET || fmt == NULL)
        return;
    size_t len;
    char buf[256 + 32];
    int off = 0;
    const char *log_tag = (tag != NULL) ? tag : "";

	off = snprintf(buf, sizeof(buf), "%s",log_tag);
    if (off < 0 || off >= (int)sizeof(buf))
        return;
    
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf + off, sizeof(buf) - off, fmt, args);
    va_end(args);
	len = strlen(buf);
	
	if(CDC_Transmit_HS((uint8_t*)buf,len) != USBD_OK)
		osDelay(1);
}




