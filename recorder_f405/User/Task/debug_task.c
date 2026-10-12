#include "debug_task.h"
#include "cmsis_os.h"
#include "task.h"
#include "data_log.h"
#include "stdint.h"
#include "log.h"
#include "usbh_cdc.h"
#include "usb_host.h"
#include "recorder.h"

#define row_debug 2 * 10
#define DEBUG_TEXT_LOG_DIV 2u
#define DEBUG_TEXT_LOG_SLOT_NUM 26u
uint8_t debug_wave = 0;
uint8_t debug_text_log_enable = 1;
float test_hex = 1;

static void debug_log_stress_data(void)
{
    static uint32_t sequence = 0;
    uint32_t pattern;

    sequence++;
    pattern = sequence * 0x9E3779B9UL;
    DEBUG_INFO("[stress] seq=%lu tick=%lu data=%08lX%08lX%08lX%08lX%08lX%08lX%08lX%08lX\r\n",
               (unsigned long)sequence,
               (unsigned long)HAL_GetTick(),
               (unsigned long)pattern,
               (unsigned long)~pattern,
               (unsigned long)(pattern ^ 0xA5A55A5AUL),
               (unsigned long)(pattern + 0x13579BDFUL),
               (unsigned long)(pattern ^ 0x3C6EF372UL),
               (unsigned long)(pattern + 0x2468ACE0UL),
               (unsigned long)~(pattern ^ 0x5A5AA5A5UL),
               (unsigned long)(pattern ^ sequence));
}

static void debug_log_text_output(void)
{
    static uint8_t log_slot = 0;

    if (!debug_text_log_enable || debug_wave != 0)
    {
        return;
    }

    switch (log_slot)
    {
    default:
        break;
    }

    log_slot = (log_slot + 1) % DEBUG_TEXT_LOG_SLOT_NUM;
}

uint8_t log_flag;



/* 串口上位机数据发送任务 */
void debug_task(void const *argument)
{
    uint32_t thread_wake_time = osKernelSysTick();
    uint8_t text_log_div = 0;

    for (;;)
    {
        thread_wake_time = osKernelSysTick();
        if (log_flag)
        {
			debug_log_text_output();
			if (debug_text_log_enable && (debug_wave == 0))
			{
				debug_log_stress_data();
			}
        }
        osDelayUntil(&thread_wake_time, 1);
    }
}
