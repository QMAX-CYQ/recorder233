#include "cmsis_os.h"
#include "log.h"
#include "usbh_cdc.h"
#include "usb_host.h"
#include "recorder.h"
uint8_t usb_buffer[512] __attribute__((aligned(4)));
extern USBH_HandleTypeDef hUsbHostHS;
extern ApplicationTypeDef Appli_state;

static volatile uint8_t usb_rx_pending;
static volatile uint8_t usb_rx_ready;
static volatile uint16_t usb_rx_len;

void USBH_CDC_ReceiveCallback(USBH_HandleTypeDef *phost)
{
    uint16_t rx_len;

    if (phost != &hUsbHostHS)
    {
        return;
    }

    rx_len = USBH_CDC_GetLastReceivedDataSize(phost);
    if (rx_len > 0U)
    {
        usb_rx_len = rx_len;
        usb_rx_ready = 1U;
    }

    usb_rx_pending = 0U;
}

void usb_task(void const *argument)
{
	uint32_t thread_wake_time = osKernelSysTick();
	for(;;)
	{
		thread_wake_time = osKernelSysTick();
		if (Appli_state == APPLICATION_READY)
        {
            CDC_HandleTypeDef *cdc_handle = NULL;
            uint32_t rx_request_len = 0U;

            if (usb_rx_ready && RecorderDataWrite((const char *)usb_buffer, usb_rx_len))
            {
                usb_rx_ready = 0U;
            }

            if (hUsbHostHS.pActiveClass != NULL)
            {
                cdc_handle = (CDC_HandleTypeDef *)hUsbHostHS.pActiveClass->pData;
                if (cdc_handle != NULL)
                {
                    rx_request_len = cdc_handle->DataItf.InEpSize;
                }
            }

            if (!usb_rx_pending && !usb_rx_ready && (rx_request_len > 0U))
            {
                usb_rx_pending = 1U;
                if (USBH_CDC_Receive(&hUsbHostHS, usb_buffer, rx_request_len) != USBH_OK)
                    usb_rx_pending = 0U;
            }
        }
        else
        {
            usb_rx_pending = 0U;
            usb_rx_ready = 0U;
        }
        osDelayUntil(&thread_wake_time, 1);
	}
}

