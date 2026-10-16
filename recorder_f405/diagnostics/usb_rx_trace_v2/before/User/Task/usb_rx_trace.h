#ifndef USB_RX_TRACE_H
#define USB_RX_TRACE_H

#include <stdint.h>

#define USB_RX_TRACE_CAPACITY 64U
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
    uint32_t event_count[USB_RX_TRACE_EVENT_LIMIT];
    usb_rx_trace_entry_t entries[USB_RX_TRACE_CAPACITY];
} usb_rx_trace_diag_t;

extern volatile usb_rx_trace_diag_t g_usb_rx_trace_diag;

/* 只读采样，支持任务和中断调用；仅记录当前 HS 实例的 Bulk IN 通道。
 * 全局连接事件使用 NO_CHANNEL，避免混入枚举控制传输及 READY 的 OUT 流量。 */
void UsbRxTrace_Record(const void *instance, uint32_t channel, uint32_t event, uint32_t detail);
void UsbRxTrace_Freeze(uint32_t request_id);
void UsbRxTrace_Resume(void);

#endif
