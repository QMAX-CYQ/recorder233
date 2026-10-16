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

/* 接收模块的启动握手统计。 */
typedef struct {
    uint32_t session, ready_attempts, ready_started, ready_completed, ack_received, tx_failed;
    uint32_t discarded_before_ready;
} log_host_link_diag_t;
extern volatile log_host_link_diag_t g_log_host_link_diag;
extern volatile uint8_t g_log_host_ack_received;

/* 仅记录一次接收请求等待过久的现场，不取消请求、不复位 USB。
 * 未握手或发送端没有数据时也可能超时，不能直接视为丢包。 */
#ifndef USB_RX_TIMEOUT_MS
#define USB_RX_TIMEOUT_MS 1000U
#endif
#define USB_RX_TIMEOUT_HISTORY_SIZE 4U

typedef struct {
    uint32_t valid, event_id, capture_tick, request_id, request_tick, pending_ms;
    uint32_t callback_count, last_callback_tick, raw_bytes;
    uint32_t application_state, host_state, host_timer;
    uint32_t pending, ready, waiting_for_ring, ring_ready, ring_free;
    uint32_t ack_received, session, ready_started, ready_completed;
    uint32_t cdc_valid, cdc_state, data_rx_state, data_tx_state;
    uint32_t in_pipe, in_ep, in_ep_size, rx_data, rx_length;
    uint32_t hc_valid, hc_state, urb_state, error_count;
    uint32_t xfer_buff, xfer_len, xfer_count, toggle_in, data_pid;
    uint32_t gahbcfg, grstctl, gintsts, gintmsk, hnptxsts;
    uint32_t hprt, hfnum, haint, haintmsk;
    uint32_t hcchar, hcint, hcintmsk, hctsiz, hcdma;
} usb_rx_timeout_snapshot_t;

typedef struct {
    uint32_t threshold_ms, request_count, callback_count, last_callback_tick;
    uint32_t active, current_pending_ms, timeout_count, ended_count;
    uint32_t last_end_tick, last_end_reason, max_observed_pending_ms;
    uint32_t history_next, latest_index;
    /* 首次现场保留到复位，环形历史保存最近四次超时。valid 最后置位。 */
    usb_rx_timeout_snapshot_t first;
    usb_rx_timeout_snapshot_t history[USB_RX_TIMEOUT_HISTORY_SIZE];
} usb_rx_timeout_diag_t;
extern volatile usb_rx_timeout_diag_t g_usb_rx_timeout_diag;

#ifndef USB_HOST_RECOVERY_TIMEOUT_MS
#define USB_HOST_RECOVERY_TIMEOUT_MS 2000U
#endif
#define USB_HOST_RECOVERY_COOLDOWN_MS 5000U
#define USB_HOST_RECOVERY_ENUM_TIMEOUT_MS 10000U
typedef enum {
    USB_HOST_RECOVERY_IDLE = 0,
    USB_HOST_RECOVERY_REQUESTED,
    USB_HOST_RECOVERY_RUNNING,
    USB_HOST_RECOVERY_WAIT_LINK,
    USB_HOST_RECOVERY_FAILED
} usb_host_recovery_state_t;
typedef struct {
    uint32_t state, requests, attempts, core_restarted, restored, failed;
    uint32_t reason, request_tick, start_tick, restart_tick, restored_tick;
    uint32_t last_duration_ms, last_status, last_hcchar, last_urb_state;
    uint32_t callback_baseline, discarded_ready_packets, discarded_ready_bytes;
} usb_host_recovery_diag_t;
extern volatile usb_host_recovery_diag_t g_usb_host_recovery_diag;
void usb_task(void const *argument);

#endif
