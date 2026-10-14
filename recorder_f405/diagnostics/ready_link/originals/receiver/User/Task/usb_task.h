#ifndef __USB_TASK_H
#define __USB_TASK_H
#include "stdint.h"

typedef struct {
    uint32_t before_tick, after_tick, local_tick, estimated_missing;
} usb_rx_gap_t;
typedef struct {
    uint32_t raw_bytes, complete_lines, tick_lines, non_tick_lines;
    uint32_t line_bytes_pending, max_line_bytes, non_crlf_lines;
    uint32_t tick_gap_count, estimated_missing, duplicate_tick, backward_tick;
    uint32_t irregular_tick_gap, last_source_tick, max_source_gap_ms;
    uint32_t gap_history_next;
    usb_rx_gap_t gap_history[8];
    uint32_t callbacks_with_data, last_callback_tick, max_callback_gap_ms;
    uint32_t max_task_gap_ms, max_pending_ms, max_ready_hold_ms, max_ring_wait_ms;
    uint32_t min_ring_free, ring_samples, format_fail, oversize_callback;
    uint32_t ready_transitions, disconnect_transitions, discarded_ready_packets;
    uint32_t discarded_partial_line_bytes;
} usb_rx_diag_t;
extern volatile usb_rx_diag_t g_usb_rx_diag;

void usb_task(void const *argument);

#endif
