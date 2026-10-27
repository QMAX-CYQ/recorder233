#include "control_def.h"
#if COMM_INTERFACE == COMM_INTERFACE_USB || COMM_INTERFACE == COMM_INTERFACE_SPI
#include "log_rx_timestamp.h"
#include "recorder_inter.h"
#include "calendar.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

uint8_t LogRxFormatTimestamp(const uint8_t *input, size_t input_len,
    uint8_t line_start, uint8_t *output, size_t capacity,
    size_t *output_len, uint8_t *next_line_start)
{
    size_t input_index, output_index = 0U;
    uint8_t at_line_start = line_start;
    if ((input == NULL && input_len != 0U) || output == NULL ||
        output_len == NULL || next_line_start == NULL) return 0U;

    for (input_index = 0U; input_index < input_len; input_index++) {
        uint8_t data = input[input_index];
        if (at_line_start && data != '\r' && data != '\n') {
            char timestamp[32];
            int timestamp_len;
#if USE_RTC
            time_t snapshot;
            /* 与原 USB 行为一致，每个新行读取独立 RTC 快照。 */
            (void)Calendar_ReadTime(&snapshot);
            timestamp_len = snprintf(timestamp, sizeof(timestamp), "%02u%02u-%02u:%02u:%02u ",
                (unsigned int)snapshot.month, (unsigned int)snapshot.day,
                (unsigned int)snapshot.hour, (unsigned int)snapshot.min,
                (unsigned int)snapshot.second);
#else
            timestamp_len = snprintf(timestamp, sizeof(timestamp), "%lu ",
                (unsigned long)HAL_GetTick());
#endif
            if (timestamp_len <= 0 || timestamp_len > LOG_RX_TIMESTAMP_MAX_PREFIX ||
                output_index >= capacity ||
                (size_t)timestamp_len >= capacity - output_index) return 0U;
            memcpy(output + output_index, timestamp, (size_t)timestamp_len);
            output_index += (size_t)timestamp_len;
            at_line_start = 0U;
        }
        if (output_index >= capacity) return 0U;
        output[output_index++] = data;
        if (data == '\n') at_line_start = 1U;
    }
    *output_len = output_index;
    *next_line_start = at_line_start;
    return 1U;
}
#endif
