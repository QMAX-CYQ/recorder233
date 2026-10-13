#include "cmsis_os.h"
#include "log.h"
#include "usbh_cdc.h"
#include "usb_host.h"
#include "recorder.h"
#include "calendar.h"
#include "recorder_inter.h"
#include <stdio.h>
#include <string.h>

uint8_t usb_buffer[512] __attribute__((aligned(4)));
#define USB_RX_TIMESTAMP_BUFFER_SIZE (sizeof(usb_buffer) * 10U)
static uint8_t usb_rx_timestamp_buffer[USB_RX_TIMESTAMP_BUFFER_SIZE];
extern USBH_HandleTypeDef hUsbHostHS;
extern ApplicationTypeDef Appli_state;
#if USE_RTC
extern time_t time;
#endif

volatile uint32_t usb_rx_callback_count;
volatile uint32_t usb_rx_zero_length_count;
volatile uint32_t usb_rx_write_fail_count;
volatile uint32_t usb_rx_ring_blocked_count;
volatile uint32_t usb_rx_written_packet_count;
volatile uint32_t usb_rx_written_byte_count;
volatile uint32_t usb_rx_receive_fail_count;
volatile uint32_t usb_rx_last_packet_len;
volatile uint32_t usb_rx_ring_free;
volatile uint32_t usb_rx_ring_ready;
volatile uint32_t usb_rx_last_receive_status;
volatile uint32_t usb_rx_request_size_invalid;

static volatile uint8_t usb_rx_pending;
static volatile uint8_t usb_rx_ready;
static volatile uint16_t usb_rx_len;
static uint8_t usb_rx_waiting_for_ring;
static uint8_t usb_rx_line_start = 1U;
static uint8_t usb_rx_formatted_line_start;
static uint8_t usb_rx_formatted_ready;
static size_t usb_rx_timestamp_len;

static uint8_t usb_format_packet_with_timestamp(uint16_t input_len, size_t *output_len,
                                                uint8_t *line_start)
{
    size_t output_index = 0U;
    uint16_t input_index;
    char timestamp[32];
    int timestamp_len;
    uint8_t at_line_start = usb_rx_line_start;

    for (input_index = 0U; input_index < input_len; input_index++)
    {
        uint8_t data = usb_buffer[input_index];

        if (at_line_start && (data != '\r') && (data != '\n'))
        {
#if USE_RTC
            GetTime();
            timestamp_len = snprintf(timestamp, sizeof(timestamp), "%02u%02u-%02u:%02u:%02u ",
                                     (unsigned int)time.month,
                                     (unsigned int)time.day,
                                     (unsigned int)time.hour,
                                     (unsigned int)time.min,
                                     (unsigned int)time.second);
#else
            timestamp_len = snprintf(timestamp, sizeof(timestamp), "%lu ",
                                     (unsigned long)HAL_GetTick());
#endif
            if ((timestamp_len <= 0) ||
                (output_index + (size_t)timestamp_len + 1U > sizeof(usb_rx_timestamp_buffer)))
            {
                return 0U;
            }

            memcpy(&usb_rx_timestamp_buffer[output_index], timestamp, (size_t)timestamp_len);
            output_index += (size_t)timestamp_len;
            at_line_start = 0U;
        }

        if (output_index >= sizeof(usb_rx_timestamp_buffer))
        {
            return 0U;
        }
        usb_rx_timestamp_buffer[output_index++] = data;

        if (data == '\n')
        {
            at_line_start = 1U;
        }
    }

    *output_len = output_index;
    *line_start = at_line_start;
    return 1U;
}

void USBH_CDC_ReceiveCallback(USBH_HandleTypeDef *phost)
{
    uint16_t rx_len;

    if (phost != &hUsbHostHS)
    {
        return;
    }

    usb_rx_callback_count++;
    rx_len = USBH_CDC_GetLastReceivedDataSize(phost);
    usb_rx_last_packet_len = rx_len;
    if (rx_len > 0U)
    {
        usb_rx_len = rx_len;
        usb_rx_ready = 1U;
    }
    else
    {
        usb_rx_zero_length_count++;
    }

    usb_rx_pending = 0U;
}

void usb_task(void const *argument)
{
    uint32_t thread_wake_time = osKernelSysTick();
    uint32_t rx_request_len;
    size_t ring_free;
    uint8_t ring_ready;
    CDC_HandleTypeDef *cdc_handle;
    USBH_StatusTypeDef receive_status;

    for (;;)
    {
        thread_wake_time = osKernelSysTick();
        if (Appli_state == APPLICATION_READY)
        {
            ring_ready = RecorderDataGetBufferStatus(&ring_free);
            usb_rx_ring_ready = ring_ready;
            usb_rx_ring_free = (uint32_t)ring_free;

            if (usb_rx_ready)
            {
                if (!usb_rx_formatted_ready)
                {
                    if (usb_format_packet_with_timestamp(usb_rx_len, &usb_rx_timestamp_len,
                                                          &usb_rx_formatted_line_start))
                    {
                        usb_rx_formatted_ready = 1U;
                    }
                }

                if (usb_rx_formatted_ready &&
                    (!ring_ready || (ring_free < usb_rx_timestamp_len)))
                {
                    if (!usb_rx_waiting_for_ring)
                    {
                        usb_rx_ring_blocked_count++;
                    }
                    usb_rx_waiting_for_ring = 1U;
                }
                else if (usb_rx_formatted_ready &&
                         RecorderDataWrite((const char *)usb_rx_timestamp_buffer, usb_rx_timestamp_len))
                {
                    usb_rx_ready = 0U;
                    usb_rx_formatted_ready = 0U;
                    usb_rx_line_start = usb_rx_formatted_line_start;
                    usb_rx_waiting_for_ring = 0U;
                    usb_rx_written_packet_count++;
                    usb_rx_written_byte_count += (uint32_t)usb_rx_timestamp_len;
                }
                else if (usb_rx_formatted_ready)
                {
                    usb_rx_write_fail_count++;
                    if (!usb_rx_waiting_for_ring)
                    {
                        usb_rx_ring_blocked_count++;
                    }
                    usb_rx_waiting_for_ring = 1U;
                }
            }

            cdc_handle = NULL;
            rx_request_len = 0U;
            if (hUsbHostHS.pActiveClass != NULL)
            {
                cdc_handle = (CDC_HandleTypeDef *)hUsbHostHS.pActiveClass->pData;
                if (cdc_handle != NULL)
                {
                    rx_request_len = cdc_handle->DataItf.InEpSize;
                }
            }

            if (rx_request_len > sizeof(usb_buffer))
            {
                usb_rx_request_size_invalid = 1U;
            }
            else if (!usb_rx_pending && !usb_rx_ready && (rx_request_len > 0U))
            {
                if (!ring_ready || (ring_free < rx_request_len))
                {
                    if (!usb_rx_waiting_for_ring)
                    {
                        usb_rx_ring_blocked_count++;
                    }
                    usb_rx_waiting_for_ring = 1U;
                }
                else
                {
                    usb_rx_waiting_for_ring = 0U;
                    receive_status = USBH_CDC_Receive(&hUsbHostHS, usb_buffer, rx_request_len);
                    usb_rx_last_receive_status = (uint32_t)receive_status;
                    if (receive_status == USBH_OK)
                    {
                        usb_rx_pending = 1U;
                    }
                    else
                    {
                        usb_rx_receive_fail_count++;
                    }
                }
            }
        }
        else
        {
            usb_rx_pending = 0U;
            usb_rx_ready = 0U;
            usb_rx_formatted_ready = 0U;
            usb_rx_line_start = 1U;
            usb_rx_waiting_for_ring = 0U;
        }
        osDelayUntil(&thread_wake_time, 1);
    }
}

