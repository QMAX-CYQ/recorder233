#ifndef USB_RX_TRACE_H
#define USB_RX_TRACE_H

#include <stdint.h>

#define USB_RX_TRACE_CAPACITY 64U
#define USB_RX_TRACE_LIFECYCLE_CAPACITY 32U
#define USB_RX_TRACE_NO_CHANNEL UINT32_MAX

/* 保存调用者返回地址，后续用同版本 AXF/MAP 定位停止通道的来源。 */
#if defined(__clang__) || defined(__GNUC__)
#define USB_RX_TRACE_CALLER() ((uint32_t)(uintptr_t)__builtin_return_address(0))
#elif defined(__CC_ARM)
#define USB_RX_TRACE_CALLER() ((uint32_t)__return_address())
#else
#define USB_RX_TRACE_CALLER() 0U
#endif

/* 事件编号固定，便于 Keil 中直接对照；detail 的解释见诊断说明。 */
typedef enum {
    USB_RX_TRACE_SUBMIT_BEFORE = 1,
    USB_RX_TRACE_SUBMIT_PREPARED,
    USB_RX_TRACE_START_BEFORE,
    USB_RX_TRACE_START_AFTER,
    USB_RX_TRACE_SUBMIT_RETURN,
    USB_RX_TRACE_IRQ_ENTER,
    USB_RX_TRACE_IRQ_EXIT,
    USB_RX_TRACE_HALT_ENTER,
    USB_RX_TRACE_HALT_EXIT,
    USB_RX_TRACE_REARM_BEFORE,
    USB_RX_TRACE_REARM_AFTER,
    USB_RX_TRACE_APP_SUBMIT,
    USB_RX_TRACE_APP_RETURN,
    USB_RX_TRACE_CALLBACK,
    USB_RX_TRACE_TIMEOUT,
    USB_RX_TRACE_CONNECT,
    USB_RX_TRACE_DISCONNECT,
    USB_RX_TRACE_PORT_ENABLED,
    USB_RX_TRACE_PORT_DISABLED,
    USB_RX_TRACE_URB_NOTIFY,
    USB_RX_TRACE_STOP_ENTER,
    USB_RX_TRACE_STOP_FLUSH_BEFORE,
    USB_RX_TRACE_STOP_FLUSH_AFTER,
    USB_RX_TRACE_STOP_HALT_BEFORE,
    USB_RX_TRACE_STOP_HALT_AFTER,
    USB_RX_TRACE_STOP_WAIT_EXIT,
    USB_RX_TRACE_STOP_EXIT,
    USB_RX_TRACE_INIT_BEFORE,
    USB_RX_TRACE_INIT_AFTER,
    USB_RX_TRACE_RECOVERY_ENTER,
    USB_RX_TRACE_RECOVERY_EXIT,
    USB_RX_TRACE_RECOVERY_FAILED,
    USB_RX_TRACE_EVENT_LIMIT
} usb_rx_trace_event_t;

typedef struct {
    uint32_t valid, sequence, tick, event, channel, detail, ipsr;
    uint32_t hcchar, hcint, hcintmsk, hctsiz, hcdma;
    uint32_t hc_state, urb_state, error_count, xfer_count, toggle_in, data_pid;
    uint32_t hfnum, gintsts, hnptxsts;
} usb_rx_trace_entry_t;

typedef struct {
    uint32_t capacity, next_index, valid_count, written, overwritten;
    uint32_t frozen, freeze_count, resume_count, freeze_tick, freeze_request_id;
    /* 主环冻结后，本生命周期环仍记录启停/重连，避免遗漏断开清理。 */
    uint32_t lifecycle_capacity, lifecycle_next_index, lifecycle_valid_count;
    uint32_t lifecycle_written, lifecycle_overwritten, sequence_next;
    uint32_t tracked_in_mask, stop_calls, stop_wait_seen_mask;
    uint32_t stop_wait_exhausted_mask, stop_wait_enabled_mask, last_stop_result;
    uint32_t event_count[USB_RX_TRACE_EVENT_LIMIT];
    usb_rx_trace_entry_t entries[USB_RX_TRACE_CAPACITY];
    usb_rx_trace_entry_t lifecycle[USB_RX_TRACE_LIFECYCLE_CAPACITY];
} usb_rx_trace_diag_t;

extern volatile usb_rx_trace_diag_t g_usb_rx_trace_diag;

/* 首次失败锁存只针对实际跟踪的 Bulk IN，避免未使用索引抢先占用现场。 */
typedef enum {
    USB_STOP_STAGE_ENTER = 0,
    USB_STOP_STAGE_FLUSH_BEFORE,
    USB_STOP_STAGE_FLUSH_AFTER,
    USB_STOP_STAGE_HALT_BEFORE,
    USB_STOP_STAGE_HALT_AFTER,
    USB_STOP_STAGE_WAIT_EXIT,
    USB_STOP_STAGE_EXIT,
    USB_STOP_STAGE_COUNT
} usb_stop_stage_t;

typedef struct {
    uint32_t tick, cycles, cycle_valid, ipsr;
    uint32_t hcchar, hcint, hcintmsk, hctsiz, hcdma;
    uint32_t hc_state, urb_state, error_count, xfer_count, toggle_in, data_pid;
    uint32_t gahbcfg, grstctl, hprt, gintsts, gintmsk, hfnum;
    uint32_t hnptxsts, hptxsts, grxfsiz, hnptxfsiz, hptxfsiz, haint, haintmsk;
} usb_stop_stage_snapshot_t;

typedef struct {
    uint32_t valid, complete, stop_call, channel, tracked_mask, caller;
    uint32_t request_id, application_state, host_state, ack_received, session;
    uint32_t core_clock_hz, cycle_counter_enabled, stage_valid_mask;
    uint32_t failure_tick, failure_count, failure_hcchar, stop_result;
    uint32_t wait_seen_mask, wait_cycle_valid_mask, exhausted_mask, enabled_mask;
    /* 原计数器是跨通道累计值，同时保存每个索引实际执行的循环次数。 */
    uint32_t wait_start_count[16], wait_end_count[16], wait_iterations[16];
    uint32_t wait_start_tick[16], wait_elapsed_ms[16];
    uint32_t wait_start_cycles[16], wait_elapsed_cycles[16];
    usb_stop_stage_snapshot_t stages[USB_STOP_STAGE_COUNT];
} usb_stop_first_failure_t;

extern volatile usb_stop_first_failure_t g_usb_stop_first_failure;

/* 只读采样，支持任务和中断调用；仅记录当前 HS 实例的 Bulk IN 通道。
 * 全局连接事件使用 NO_CHANNEL，避免混入枚举控制传输及 READY 的 OUT 流量。 */
void UsbRxTrace_Record(const void *instance, uint32_t channel, uint32_t event, uint32_t detail);
void UsbRxTrace_Freeze(uint32_t request_id);
void UsbRxTrace_Resume(void);
/* 初始化前更新接收通道跟踪，停止函数改写 EPDIR 后仍能抓到同一通道。 */
void UsbRxTrace_ChannelInit(const void *instance, uint32_t channel, uint32_t epnum, uint32_t ep_type);
/* 对全部 16 个硬件通道统计原等待循环的结果，不修改循环计数或返回值。 */
void UsbRxTrace_StopWait(const void *instance, uint32_t channel, uint32_t count);
void UsbRxTrace_StopWaitBegin(const void *instance, uint32_t channel, uint32_t count);

#endif
