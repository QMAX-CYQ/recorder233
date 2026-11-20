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


extern FRESULT isSdCardInit;
extern pcfile_status_t pcfile_status;
extern SD_HandleTypeDef hsd;

static void debug_log_sd_status(void)
{
	log_printf("[sd-status] isSdCardInit=%u g_recorder_diag.mount_result=%u g_recorder_diag.card_not_ready=%u\
	pcfile_status.state=%u hsd.State=%u hsd.ErrorCode=%u",
	isSdCardInit,g_recorder_diag.mount_result,g_recorder_diag.card_not_ready,
	pcfile_status.state,hsd.State,hsd.ErrorCode);
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
		
		case 0:
		{
			debug_log_sd_status();
			break;
		}
		default:
			break;
    }

    log_slot = (log_slot + 1) % DEBUG_TEXT_LOG_SLOT_NUM;
}

void debug_task(void const *argument)
{
    uint32_t thread_wake_time = osKernelSysTick();
    uint8_t text_log_div = 0;

    for (;;)
    {
        thread_wake_time = osKernelSysTick();
        debug_log_text_output();
        osDelayUntil(&thread_wake_time, 100);
    }
}
