#include "cmsis_os.h"
#include "log.h"
#include "usbh_cdc.h"
#include "usb_host.h"
#include "recorder.h"
#include "calendar.h"
#include "recorder_inter.h"
#include "usb_task.h"
#include <stdio.h>
#include <string.h>
#include "log_link_protocol.h"
#include "usb_rx_trace.h"

extern USBH_HandleTypeDef hUsbHostHS;
volatile log_host_link_diag_t g_log_host_link_diag;
volatile uint8_t g_log_host_ack_received;
static volatile uint8_t log_host_tx_pending;
static uint8_t log_host_rx_armed;
static volatile uint8_t log_host_session_active;
static uint32_t log_host_last_ready_tick;
static uint32_t log_host_session_counter;
static uint8_t log_host_ready_frame[16];
static log_link_parser_t log_host_parser;
static uint8_t log_host_filtered[512U+16U];

static void log_host_on_frame(uint8_t type, uint32_t session)
{
    if (type == LOG_LINK_ACK && log_host_session_active &&
        session == g_log_host_link_diag.session) {
        if (!g_log_host_ack_received) g_log_host_link_diag.ack_received++;
        g_log_host_ack_received = 1U;
    }
}

void USBH_CDC_TransmitCallback(USBH_HandleTypeDef *phost)
{
    if (phost == &hUsbHostHS) {
        log_host_tx_pending = 0U;
        g_log_host_link_diag.ready_completed++;
    }
}

uint8_t usb_buffer[512] __attribute__((aligned(4)));
#define USB_RX_TIMESTAMP_BUFFER_SIZE (sizeof(usb_buffer) * 10U)
static uint8_t usb_rx_timestamp_buffer[USB_RX_TIMESTAMP_BUFFER_SIZE];
extern USBH_HandleTypeDef hUsbHostHS;
extern ApplicationTypeDef Appli_state;

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

volatile usb_rx_diag_t g_usb_rx_diag;
static char diagnostic_line_prefix[64];
static uint32_t diagnostic_prefix_len;
static uint32_t diagnostic_line_bytes;
static uint8_t diagnostic_previous_byte;
static uint8_t diagnostic_have_tick;
static uint32_t diagnostic_request_tick;
static volatile uint32_t diagnostic_ready_tick;

extern HCD_HandleTypeDef hhcd_USB_OTG_HS;
volatile usb_rx_timeout_diag_t g_usb_rx_timeout_diag = {
    .threshold_ms = USB_RX_TIMEOUT_MS
};
static uint32_t diagnostic_timeout_request_id;

volatile usb_rx_trace_diag_t g_usb_rx_trace_diag = {
    .capacity = USB_RX_TRACE_CAPACITY,
    .lifecycle_capacity = USB_RX_TRACE_LIFECYCLE_CAPACITY
};

volatile usb_stop_first_failure_t g_usb_stop_first_failure;

static uint32_t diagnostic_stop_cycle_valid(void)
{
    return ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) != 0U &&
            (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U &&
            (DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) == 0U) ? 1U : 0U;
}

/* 仅启用诊断周期计数，不清零已有 CYCCNT，也不使用 HAL 延时。 */
static void diagnostic_stop_cycle_init(void)
{
    if ((DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) == 0U) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        __DSB();
        __ISB();
    }
}

/* 调用者已保存并关闭中断；硬件仍运行，以下寄存器不是同时锁存。 */
static void diagnostic_stop_sample(const USB_OTG_GlobalTypeDef *usb, uint32_t stage)
{
    uint32_t channel = g_usb_stop_first_failure.channel;
    volatile usb_stop_stage_snapshot_t *s = &g_usb_stop_first_failure.stages[stage];
    const USB_OTG_HostTypeDef *host =
        (const USB_OTG_HostTypeDef *)((uintptr_t)usb + USB_OTG_HOST_BASE);
    const USB_OTG_HostChannelTypeDef *regs =
        (const USB_OTG_HostChannelTypeDef *)((uintptr_t)usb +
            USB_OTG_HOST_CHANNEL_BASE + channel * USB_OTG_HOST_CHANNEL_SIZE);
    const USB_OTG_HCTypeDef *hc = &hhcd_USB_OTG_HS.hc[channel];

    s->cycles = DWT->CYCCNT;
    s->cycle_valid = diagnostic_stop_cycle_valid();
    s->tick = HAL_GetTick();
    s->ipsr = __get_IPSR();
    s->hcchar = regs->HCCHAR;
    s->hcint = regs->HCINT;
    s->hcintmsk = regs->HCINTMSK;
    s->hctsiz = regs->HCTSIZ;
    s->hcdma = regs->HCDMA;
    s->hc_state = (uint32_t)hc->state;
    s->urb_state = (uint32_t)hc->urb_state;
    s->error_count = hc->ErrCnt;
    s->xfer_count = hc->xfer_count;
    s->toggle_in = hc->toggle_in;
    s->data_pid = hc->data_pid;
    s->gahbcfg = usb->GAHBCFG;
    s->grstctl = usb->GRSTCTL;
    s->hprt = *(volatile uint32_t *)((uintptr_t)usb + USB_OTG_HOST_PORT_BASE);
    s->gintsts = usb->GINTSTS;
    s->gintmsk = usb->GINTMSK;
    s->hfnum = host->HFNUM;
    s->hnptxsts = usb->HNPTXSTS;
    s->hptxsts = host->HPTXSTS;
    s->grxfsiz = usb->GRXFSIZ;
    s->hnptxfsiz = usb->DIEPTXF0_HNPTXFSIZ;
    s->hptxfsiz = usb->HPTXFSIZ;
    s->haint = host->HAINT;
    s->haintmsk = host->HAINTMSK;
    __DMB();
    g_usb_stop_first_failure.stage_valid_mask |= 1UL << stage;
}

static uint32_t diagnostic_stop_attempt_active(void)
{
    return g_usb_stop_first_failure.stage_valid_mask != 0U &&
           g_usb_stop_first_failure.stop_call == g_usb_rx_trace_diag.stop_calls &&
           !g_usb_stop_first_failure.complete;
}

static void diagnostic_stop_event(const USB_OTG_GlobalTypeDef *usb,
                                  uint32_t channel, uint32_t event, uint32_t detail)
{
    uint32_t i, mask, stage;
    if (event == USB_RX_TRACE_STOP_ENTER) {
        if (g_usb_stop_first_failure.valid) return;
        mask = g_usb_rx_trace_diag.tracked_in_mask;
        for (i = 0U; i < hhcd_USB_OTG_HS.Init.Host_channels &&
             i < sizeof(hhcd_USB_OTG_HS.hc) / sizeof(hhcd_USB_OTG_HS.hc[0]); i++) {
            if ((mask & (1UL << i)) != 0U) break;
        }
        if (i >= hhcd_USB_OTG_HS.Init.Host_channels ||
            i >= sizeof(hhcd_USB_OTG_HS.hc) / sizeof(hhcd_USB_OTG_HS.hc[0])) return;
        diagnostic_stop_cycle_init();
        /* 未失败的候选现场可复用；首次失败 valid 置位后不再清除。 */
        memset((void *)&g_usb_stop_first_failure, 0, sizeof(g_usb_stop_first_failure));
        g_usb_stop_first_failure.stop_call = g_usb_rx_trace_diag.stop_calls;
        g_usb_stop_first_failure.channel = i;
        g_usb_stop_first_failure.tracked_mask = mask;
        g_usb_stop_first_failure.caller = detail;
        g_usb_stop_first_failure.request_id = g_usb_rx_timeout_diag.request_count;
        g_usb_stop_first_failure.application_state = (uint32_t)Appli_state;
        g_usb_stop_first_failure.host_state = (uint32_t)hUsbHostHS.gState;
        g_usb_stop_first_failure.ack_received = g_log_host_ack_received;
        g_usb_stop_first_failure.session = g_log_host_link_diag.session;
        g_usb_stop_first_failure.core_clock_hz = SystemCoreClock;
        g_usb_stop_first_failure.cycle_counter_enabled = diagnostic_stop_cycle_valid();
        g_usb_stop_first_failure.stop_result = UINT32_MAX;
        diagnostic_stop_sample(usb, USB_STOP_STAGE_ENTER);
        return;
    }
    if (!diagnostic_stop_attempt_active()) return;
    if (event == USB_RX_TRACE_STOP_EXIT) {
        diagnostic_stop_sample(usb, USB_STOP_STAGE_EXIT);
        g_usb_stop_first_failure.stop_result = detail;
        g_usb_stop_first_failure.exhausted_mask = g_usb_rx_trace_diag.stop_wait_exhausted_mask;
        g_usb_stop_first_failure.enabled_mask = g_usb_rx_trace_diag.stop_wait_enabled_mask;
        __DMB();
        g_usb_stop_first_failure.complete = 1U;
        return;
    }
    if (channel != g_usb_stop_first_failure.channel) return;
    switch (event) {
        case USB_RX_TRACE_STOP_FLUSH_BEFORE: stage = USB_STOP_STAGE_FLUSH_BEFORE; break;
        case USB_RX_TRACE_STOP_FLUSH_AFTER: stage = USB_STOP_STAGE_FLUSH_AFTER; break;
        case USB_RX_TRACE_STOP_HALT_BEFORE: stage = USB_STOP_STAGE_HALT_BEFORE; break;
        case USB_RX_TRACE_STOP_HALT_AFTER: stage = USB_STOP_STAGE_HALT_AFTER; break;
        default: return;
    }
    diagnostic_stop_sample(usb, stage);
}

void UsbRxTrace_Record(const void *instance, uint32_t channel, uint32_t event, uint32_t detail)
{
    const USB_OTG_GlobalTypeDef *usb = (const USB_OTG_GlobalTypeDef *)instance;
    const USB_OTG_HostTypeDef *host;
    const USB_OTG_HostChannelTypeDef *regs = NULL;
    const USB_OTG_HCTypeDef *hc = NULL;
    volatile usb_rx_trace_entry_t *entry;
    volatile usb_rx_trace_entry_t *lifecycle_entry = NULL;
    uint32_t primask, slot, is_lifecycle, write_main;

    if (usb == NULL || usb != hhcd_USB_OTG_HS.Instance ||
        event == 0U || event >= USB_RX_TRACE_EVENT_LIMIT) return;
    primask = __get_PRIMASK();
    __disable_irq();
    is_lifecycle = event >= USB_RX_TRACE_STOP_ENTER ||
        (event >= USB_RX_TRACE_TIMEOUT && event <= USB_RX_TRACE_PORT_DISABLED);
    write_main = !g_usb_rx_trace_diag.frozen;
    if (!write_main && !is_lifecycle) {
        __set_PRIMASK(primask);
        return;
    }
    if (channel != USB_RX_TRACE_NO_CHANNEL) {
        if (channel >= hhcd_USB_OTG_HS.Init.Host_channels ||
            channel >= sizeof(hhcd_USB_OTG_HS.hc) / sizeof(hhcd_USB_OTG_HS.hc[0])) {
            __set_PRIMASK(primask);
            return;
        }
        regs = (const USB_OTG_HostChannelTypeDef *)((uintptr_t)usb +
            USB_OTG_HOST_CHANNEL_BASE + channel * USB_OTG_HOST_CHANNEL_SIZE);
        /* StopHost 会清 EPDIR，已跟踪通道仍需继续记录，不固定通道 4。 */
        if ((regs->HCCHAR & (USB_OTG_HCCHAR_EPDIR | USB_OTG_HCCHAR_EPTYP)) !=
            (USB_OTG_HCCHAR_EPDIR | (2U << USB_OTG_HCCHAR_EPTYP_Pos)) &&
            !(g_usb_rx_trace_diag.tracked_in_mask & (1UL << channel))) {
            __set_PRIMASK(primask);
            return;
        }
        g_usb_rx_trace_diag.tracked_in_mask |= 1UL << channel;
        hc = &hhcd_USB_OTG_HS.hc[channel];
    }
    if (event == USB_RX_TRACE_STOP_ENTER) {
        g_usb_rx_trace_diag.stop_calls++;
        g_usb_rx_trace_diag.stop_wait_seen_mask = 0U;
        g_usb_rx_trace_diag.stop_wait_exhausted_mask = 0U;
        g_usb_rx_trace_diag.stop_wait_enabled_mask = 0U;
        g_usb_rx_trace_diag.last_stop_result = UINT32_MAX;
    } else if (event == USB_RX_TRACE_STOP_EXIT) {
        g_usb_rx_trace_diag.last_stop_result = detail;
    }
    diagnostic_stop_event(usb, channel, event, detail);
    slot = g_usb_rx_trace_diag.next_index;
    if (is_lifecycle)
        lifecycle_entry = &g_usb_rx_trace_diag.lifecycle[g_usb_rx_trace_diag.lifecycle_next_index];
    entry = write_main ? &g_usb_rx_trace_diag.entries[slot] : lifecycle_entry;
    entry->valid = 0U;
    entry->sequence = g_usb_rx_trace_diag.sequence_next++;
    entry->tick = HAL_GetTick();
    entry->event = event;
    entry->channel = channel;
    entry->detail = detail;
    entry->ipsr = __get_IPSR();
    entry->hcchar = regs != NULL ? regs->HCCHAR : 0U;
    entry->hcint = regs != NULL ? regs->HCINT : 0U;
    entry->hcintmsk = regs != NULL ? regs->HCINTMSK : 0U;
    entry->hctsiz = regs != NULL ? regs->HCTSIZ : 0U;
    entry->hcdma = regs != NULL ? regs->HCDMA : 0U;
    entry->hc_state = hc != NULL ? (uint32_t)hc->state : 0U;
    entry->urb_state = hc != NULL ? (uint32_t)hc->urb_state : 0U;
    entry->error_count = hc != NULL ? hc->ErrCnt : 0U;
    entry->xfer_count = hc != NULL ? hc->xfer_count : 0U;
    entry->toggle_in = hc != NULL ? hc->toggle_in : 0U;
    entry->data_pid = hc != NULL ? hc->data_pid : 0U;
    host = (const USB_OTG_HostTypeDef *)((uintptr_t)usb + USB_OTG_HOST_BASE);
    entry->hfnum = host->HFNUM;
    entry->gintsts = usb->GINTSTS;
    entry->hnptxsts = usb->HNPTXSTS;
    if (write_main && is_lifecycle) {
        lifecycle_entry->valid = 0U;
        *lifecycle_entry = *entry;
    }
    __DMB();
    entry->valid = 1U;
    if (is_lifecycle) {
        lifecycle_entry->valid = 1U;
        g_usb_rx_trace_diag.lifecycle_written++;
        if (g_usb_rx_trace_diag.lifecycle_valid_count < USB_RX_TRACE_LIFECYCLE_CAPACITY)
            g_usb_rx_trace_diag.lifecycle_valid_count++;
        else g_usb_rx_trace_diag.lifecycle_overwritten++;
        g_usb_rx_trace_diag.lifecycle_next_index =
            (g_usb_rx_trace_diag.lifecycle_next_index + 1U) % USB_RX_TRACE_LIFECYCLE_CAPACITY;
    }
    if (write_main) {
        g_usb_rx_trace_diag.written++;
        if (g_usb_rx_trace_diag.valid_count < USB_RX_TRACE_CAPACITY)
            g_usb_rx_trace_diag.valid_count++;
        else g_usb_rx_trace_diag.overwritten++;
        g_usb_rx_trace_diag.next_index = (slot + 1U) % USB_RX_TRACE_CAPACITY;
    }
    g_usb_rx_trace_diag.event_count[event]++;
    __set_PRIMASK(primask);
}

void UsbRxTrace_ChannelInit(const void *instance, uint32_t channel, uint32_t epnum, uint32_t ep_type)
{
    uint32_t primask;
    if (instance == NULL || instance != hhcd_USB_OTG_HS.Instance ||
        channel >= hhcd_USB_OTG_HS.Init.Host_channels ||
        channel >= sizeof(hhcd_USB_OTG_HS.hc) / sizeof(hhcd_USB_OTG_HS.hc[0])) return;
    primask = __get_PRIMASK();
    __disable_irq();
    if (ep_type == 2U && (epnum & 0x80U) != 0U) {
        g_usb_rx_trace_diag.tracked_in_mask |= 1UL << channel;
        UsbRxTrace_Record(instance, channel, USB_RX_TRACE_INIT_BEFORE, (ep_type << 8) | epnum);
    } else {
        g_usb_rx_trace_diag.tracked_in_mask &= ~(1UL << channel);
    }
    __set_PRIMASK(primask);
}

void UsbRxTrace_StopWait(const void *instance, uint32_t channel, uint32_t count)
{
    const USB_OTG_HostChannelTypeDef *regs;
    uint32_t primask, enabled, hcchar, now_cycles, now_tick;
    if (instance == NULL || instance != hhcd_USB_OTG_HS.Instance || channel >= 16U) return;
    regs = (const USB_OTG_HostChannelTypeDef *)((uintptr_t)instance +
        USB_OTG_HOST_CHANNEL_BASE + channel * USB_OTG_HOST_CHANNEL_SIZE);
    primask = __get_PRIMASK();
    __disable_irq();
    now_cycles = DWT->CYCCNT;
    now_tick = HAL_GetTick();
    hcchar = regs->HCCHAR;
    enabled = hcchar & USB_OTG_HCCHAR_CHENA;
    g_usb_rx_trace_diag.stop_wait_seen_mask |= 1UL << channel;
    if (enabled != 0U) {
        g_usb_rx_trace_diag.stop_wait_enabled_mask |= 1UL << channel;
        if (count > 1000U) g_usb_rx_trace_diag.stop_wait_exhausted_mask |= 1UL << channel;
    }
    if (diagnostic_stop_attempt_active()) {
        g_usb_stop_first_failure.wait_end_count[channel] = count;
        g_usb_stop_first_failure.wait_seen_mask |= 1UL << channel;
        g_usb_stop_first_failure.wait_iterations[channel] =
            count - g_usb_stop_first_failure.wait_start_count[channel];
        g_usb_stop_first_failure.wait_elapsed_ms[channel] =
            now_tick - g_usb_stop_first_failure.wait_start_tick[channel];
        g_usb_stop_first_failure.wait_elapsed_cycles[channel] =
            now_cycles - g_usb_stop_first_failure.wait_start_cycles[channel];
        if (!diagnostic_stop_cycle_valid())
            g_usb_stop_first_failure.wait_cycle_valid_mask &= ~(1UL << channel);
        if (channel == g_usb_stop_first_failure.channel) {
            diagnostic_stop_sample((const USB_OTG_GlobalTypeDef *)instance, USB_STOP_STAGE_WAIT_EXIT);
            if (count > 1000U && enabled != 0U) {
                g_usb_stop_first_failure.failure_tick = now_tick;
                g_usb_stop_first_failure.failure_count = count;
                g_usb_stop_first_failure.failure_hcchar = hcchar;
                __DMB();
                g_usb_stop_first_failure.valid = 1U;
            }
        }
    }
    /* detail 保存原循环计数，包含原驱动跨通道共用计数器的实际结果。 */
    UsbRxTrace_Record(instance, channel, USB_RX_TRACE_STOP_WAIT_EXIT, count);
    __set_PRIMASK(primask);
}

void UsbRxTrace_StopWaitBegin(const void *instance, uint32_t channel, uint32_t count)
{
    uint32_t primask;
    if (instance == NULL || instance != hhcd_USB_OTG_HS.Instance || channel >= 16U) return;
    primask = __get_PRIMASK();
    __disable_irq();
    if (diagnostic_stop_attempt_active()) {
        g_usb_stop_first_failure.wait_start_count[channel] = count;
        g_usb_stop_first_failure.wait_start_tick[channel] = HAL_GetTick();
        g_usb_stop_first_failure.wait_start_cycles[channel] = DWT->CYCCNT;
        if (diagnostic_stop_cycle_valid())
            g_usb_stop_first_failure.wait_cycle_valid_mask |= 1UL << channel;
    }
    __set_PRIMASK(primask);
}

void UsbRxTrace_Freeze(uint32_t request_id)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (!g_usb_rx_trace_diag.frozen) {
        UsbRxTrace_Record(hhcd_USB_OTG_HS.Instance, USB_RX_TRACE_NO_CHANNEL,
                          USB_RX_TRACE_TIMEOUT, request_id);
        g_usb_rx_trace_diag.freeze_tick = HAL_GetTick();
        g_usb_rx_trace_diag.freeze_request_id = request_id;
        g_usb_rx_trace_diag.freeze_count++;
        g_usb_rx_trace_diag.frozen = 1U;
    }
    __set_PRIMASK(primask);
}

void UsbRxTrace_Resume(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (g_usb_rx_trace_diag.frozen) {
        g_usb_rx_trace_diag.frozen = 0U;
        g_usb_rx_trace_diag.resume_count++;
    }
    __set_PRIMASK(primask);
}

/* 仅在超时首次出现时读取寄存器；不清中断、不停止通道、不改变接收状态。 */
static void diagnostic_capture_timeout(uint32_t request_id)
{
    usb_rx_timeout_snapshot_t snapshot = {0};
    CDC_HandleTypeDef *cdc;
    USB_OTG_GlobalTypeDef *usb = hhcd_USB_OTG_HS.Instance;
    USB_OTG_HostTypeDef *host;
    USB_OTG_HostChannelTypeDef *channel;
    USB_OTG_HCTypeDef *hc;
    uint32_t primask, slot, pipe;

    primask = __get_PRIMASK();
    __disable_irq();
    /* 回调可能在判断超时后到达，锁内重新确认，避免记录已完成的旧请求。 */
    if (!usb_rx_pending || Appli_state != APPLICATION_READY ||
        request_id != g_usb_rx_timeout_diag.request_count ||
        hUsbHostHS.pActiveClass == NULL || usb == NULL) {
        __set_PRIMASK(primask);
        return;
    }
    cdc = (CDC_HandleTypeDef *)hUsbHostHS.pActiveClass->pData;
    snapshot.event_id = g_usb_rx_timeout_diag.timeout_count + 1U;
    snapshot.capture_tick = HAL_GetTick();
    snapshot.request_id = request_id;
    snapshot.request_tick = diagnostic_request_tick;
    snapshot.pending_ms = snapshot.capture_tick - snapshot.request_tick;
    snapshot.callback_count = usb_rx_callback_count;
    snapshot.last_callback_tick = g_usb_rx_timeout_diag.last_callback_tick;
    snapshot.raw_bytes = g_usb_rx_diag.raw_bytes;
    snapshot.application_state = (uint32_t)Appli_state;
    snapshot.host_state = (uint32_t)hUsbHostHS.gState;
    snapshot.host_timer = hUsbHostHS.Timer;
    snapshot.pending = usb_rx_pending;
    snapshot.ready = usb_rx_ready;
    snapshot.waiting_for_ring = usb_rx_waiting_for_ring;
    snapshot.ring_ready = usb_rx_ring_ready;
    snapshot.ring_free = usb_rx_ring_free;
    snapshot.ack_received = g_log_host_ack_received;
    snapshot.session = g_log_host_link_diag.session;
    snapshot.ready_started = g_log_host_link_diag.ready_started;
    snapshot.ready_completed = g_log_host_link_diag.ready_completed;
    host = (USB_OTG_HostTypeDef *)((uintptr_t)usb + USB_OTG_HOST_BASE);
    snapshot.gahbcfg = usb->GAHBCFG;
    snapshot.grstctl = usb->GRSTCTL;
    snapshot.gintsts = usb->GINTSTS;
    snapshot.gintmsk = usb->GINTMSK;
    snapshot.hnptxsts = usb->HNPTXSTS;
    snapshot.hprt = *(volatile uint32_t *)((uintptr_t)usb + USB_OTG_HOST_PORT_BASE);
    snapshot.hfnum = host->HFNUM;
    snapshot.haint = host->HAINT;
    snapshot.haintmsk = host->HAINTMSK;
    if (cdc != NULL) {
        snapshot.cdc_valid = 1U;
        snapshot.cdc_state = (uint32_t)cdc->state;
        snapshot.data_rx_state = (uint32_t)cdc->data_rx_state;
        snapshot.data_tx_state = (uint32_t)cdc->data_tx_state;
        snapshot.in_pipe = cdc->DataItf.InPipe;
        snapshot.in_ep = cdc->DataItf.InEp;
        snapshot.in_ep_size = cdc->DataItf.InEpSize;
        snapshot.rx_data = (uint32_t)(uintptr_t)cdc->pRxData;
        snapshot.rx_length = cdc->RxDataLength;
        pipe = cdc->DataItf.InPipe;
        if (pipe < hhcd_USB_OTG_HS.Init.Host_channels &&
            pipe < sizeof(hhcd_USB_OTG_HS.hc) / sizeof(hhcd_USB_OTG_HS.hc[0])) {
            hc = &hhcd_USB_OTG_HS.hc[pipe];
            channel = (USB_OTG_HostChannelTypeDef *)((uintptr_t)usb +
                USB_OTG_HOST_CHANNEL_BASE + pipe * USB_OTG_HOST_CHANNEL_SIZE);
            snapshot.hc_valid = 1U;
            snapshot.hc_state = (uint32_t)hc->state;
            snapshot.urb_state = (uint32_t)hc->urb_state;
            snapshot.error_count = hc->ErrCnt;
            snapshot.xfer_buff = (uint32_t)(uintptr_t)hc->xfer_buff;
            snapshot.xfer_len = hc->xfer_len;
            snapshot.xfer_count = hc->xfer_count;
            snapshot.toggle_in = hc->toggle_in;
            snapshot.data_pid = hc->data_pid;
            snapshot.hcchar = channel->HCCHAR;
            snapshot.hcint = channel->HCINT;
            snapshot.hcintmsk = channel->HCINTMSK;
            snapshot.hctsiz = channel->HCTSIZ;
            snapshot.hcdma = channel->HCDMA;
        }
    }
    /* 主环冻结后，独立生命周期环继续保留断开、StopHost 和重新初始化。 */
    UsbRxTrace_Freeze(request_id);
    /* CPU 状态在短临界区内采样；USB 硬件仍运行，寄存器并非同时锁存。 */
    __set_PRIMASK(primask);

    slot = g_usb_rx_timeout_diag.history_next % USB_RX_TIMEOUT_HISTORY_SIZE;
    g_usb_rx_timeout_diag.history[slot].valid = 0U;
    g_usb_rx_timeout_diag.history[slot] = snapshot;
    __DMB();
    g_usb_rx_timeout_diag.history[slot].valid = 1U;
    if (!g_usb_rx_timeout_diag.first.valid) {
        g_usb_rx_timeout_diag.first = snapshot;
        __DMB();
        g_usb_rx_timeout_diag.first.valid = 1U;
    }
    diagnostic_timeout_request_id = request_id;
    g_usb_rx_timeout_diag.latest_index = slot;
    g_usb_rx_timeout_diag.history_next++;
    g_usb_rx_timeout_diag.timeout_count++;
    g_usb_rx_timeout_diag.active = 1U;
}

static void diagnostic_poll_timeout(void)
{
    uint32_t elapsed, request_id = g_usb_rx_timeout_diag.request_count;
    uint32_t callback_count = usb_rx_callback_count;
    uint32_t request_tick = diagnostic_request_tick;
    uint32_t now = HAL_GetTick();

    if (g_usb_rx_timeout_diag.active &&
        (!usb_rx_pending || Appli_state != APPLICATION_READY ||
         request_id != diagnostic_timeout_request_id)) {
        const volatile usb_rx_timeout_snapshot_t *last =
            &g_usb_rx_timeout_diag.history[g_usb_rx_timeout_diag.latest_index];
        g_usb_rx_timeout_diag.active = 0U;
        g_usb_rx_timeout_diag.ended_count++;
        g_usb_rx_timeout_diag.last_end_tick = now;
        /* 1：回调后结束；2：离开就绪态；3：请求被清除或替换。 */
        g_usb_rx_timeout_diag.last_end_reason = Appli_state != APPLICATION_READY ? 2U :
            (callback_count != last->callback_count ? 1U : 3U);
        /* 正常静默也可能超时，观察到请求结束后继续记录下一次故障。 */
        UsbRxTrace_Resume();
    }
    g_usb_rx_timeout_diag.current_pending_ms = 0U;
    if (usb_rx_pending && Appli_state == APPLICATION_READY) {
        elapsed = now - request_tick;
        g_usb_rx_timeout_diag.current_pending_ms = elapsed;
        if (elapsed > g_usb_rx_timeout_diag.max_observed_pending_ms)
            g_usb_rx_timeout_diag.max_observed_pending_ms = elapsed;
        if (!g_usb_rx_timeout_diag.active && elapsed >= USB_RX_TIMEOUT_MS)
            diagnostic_capture_timeout(request_id);
    }
}

/* Observe raw CDC bytes once in the callback, before timestamps are inserted.
 * Keep a short prefix across USB packets; never change or consume input data. */
static void diagnostic_observe_rx(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    for (i = 0U; i < len; i++) {
        uint8_t ch = data[i];
        if (ch == '\n') {
            char *tick_text;
            uint32_t source_tick = 0U;
            uint8_t have_digits = 0U;
            g_usb_rx_diag.complete_lines++;
            if (diagnostic_previous_byte != '\r') g_usb_rx_diag.non_crlf_lines++;
            if (diagnostic_line_bytes > g_usb_rx_diag.max_line_bytes)
                g_usb_rx_diag.max_line_bytes = diagnostic_line_bytes;
            diagnostic_line_prefix[diagnostic_prefix_len] = '\0';
            tick_text = NULL;
            if (strncmp(diagnostic_line_prefix, "INFO: [", 7U) == 0) {
                tick_text = strstr(diagnostic_line_prefix, "] tick=");
                if (tick_text != NULL) tick_text += 7;
                else {
                    tick_text = strstr(diagnostic_line_prefix, "] t=");
                    if (tick_text != NULL) tick_text += 4;
                }
            }
            if (tick_text != NULL) {
                while ((*tick_text >= '0') && (*tick_text <= '9')) {
                    uint32_t digit = (uint32_t)(*tick_text - '0');
                    if (source_tick > (UINT32_MAX - digit) / 10U) {
                        have_digits = 0U;
                        break;
                    }
                    source_tick = source_tick * 10U + digit;
                    have_digits = 1U;
                    tick_text++;
                }
                if (*tick_text != ' ') have_digits = 0U;
            }
            if (have_digits) {
                g_usb_rx_diag.tick_lines++;
                if (diagnostic_have_tick) {
                    uint32_t delta = source_tick - g_usb_rx_diag.last_source_tick;
                    if (delta == 0U) g_usb_rx_diag.duplicate_tick++;
                    else if (delta > UINT32_MAX / 2U) g_usb_rx_diag.backward_tick++;
                    else {
                        if (delta > g_usb_rx_diag.max_source_gap_ms)
                            g_usb_rx_diag.max_source_gap_ms = delta;
                        if ((delta % 10U) != 0U) g_usb_rx_diag.irregular_tick_gap++;
                        if (delta > 10U) {
                            uint32_t slot = g_usb_rx_diag.gap_history_next % 8U;
                            uint32_t missing = ((delta % 10U) == 0U) ? delta / 10U - 1U : 0U;
                            g_usb_rx_diag.tick_gap_count++;
                            g_usb_rx_diag.estimated_missing += missing;
                            g_usb_rx_diag.gap_history[slot].before_tick = g_usb_rx_diag.last_source_tick;
                            g_usb_rx_diag.gap_history[slot].after_tick = source_tick;
                            g_usb_rx_diag.gap_history[slot].local_tick = HAL_GetTick();
                            g_usb_rx_diag.gap_history[slot].estimated_missing = missing;
                            g_usb_rx_diag.gap_history_next++;
                        }
                    }
                }
                diagnostic_have_tick = 1U;
                g_usb_rx_diag.last_source_tick = source_tick;
            } else g_usb_rx_diag.non_tick_lines++;
            diagnostic_prefix_len = 0U;
            diagnostic_line_bytes = 0U;
        } else {
            diagnostic_line_bytes++;
            if (diagnostic_prefix_len < sizeof(diagnostic_line_prefix) - 1U)
                diagnostic_line_prefix[diagnostic_prefix_len++] = (char)ch;
        }
        diagnostic_previous_byte = ch;
    }
    g_usb_rx_diag.line_bytes_pending = diagnostic_line_bytes;
}

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
            time_t snapshot;
            /* 格式化只读取本次局部快照，避免全局日历被其他任务更新。 */
            (void)Calendar_ReadTime(&snapshot);
            timestamp_len = snprintf(timestamp, sizeof(timestamp), "%02u%02u-%02u:%02u:%02u ",
                                     (unsigned int)snapshot.month,
                                     (unsigned int)snapshot.day,
                                     (unsigned int)snapshot.hour,
                                     (unsigned int)snapshot.min,
                                     (unsigned int)snapshot.second);
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
    if (phost->pActiveClass != NULL && phost->pActiveClass->pData != NULL) {
        CDC_HandleTypeDef *trace_cdc = (CDC_HandleTypeDef *)phost->pActiveClass->pData;
        UsbRxTrace_Record(hhcd_USB_OTG_HS.Instance, trace_cdc->DataItf.InPipe,
                          USB_RX_TRACE_CALLBACK, usb_rx_callback_count);
    }
    /* 包括握手控制帧和零长度包，记录所有接收完成回调。 */
    g_usb_rx_timeout_diag.callback_count++;
    g_usb_rx_timeout_diag.last_callback_tick = HAL_GetTick();
    rx_len = USBH_CDC_GetLastReceivedDataSize(phost);
    {
        size_t filtered;
        uint32_t discarded_before = log_host_parser.discarded;
        uint16_t input_len = rx_len > sizeof(usb_buffer) ? sizeof(usb_buffer) : rx_len;
        filtered = LogLinkFilter(&log_host_parser, usb_buffer, input_len,
            log_host_filtered, &g_log_host_ack_received, log_host_on_frame);
        g_log_host_link_diag.discarded_before_ready += log_host_parser.discarded - discarded_before;
        /* 当前接收请求为 64 字节，暂存前缀最多追加 15 字节，512 字节足够。 */
        if (filtered <= sizeof(usb_buffer)) {
            memcpy(usb_buffer, log_host_filtered, filtered);
            rx_len = (uint16_t)filtered;
        } else rx_len = 0U;
    }
    usb_rx_last_packet_len = rx_len;
    if (rx_len > 0U)
    {
        uint32_t now = HAL_GetTick();
        uint32_t gap = now - g_usb_rx_diag.last_callback_tick;
        if ((g_usb_rx_diag.callbacks_with_data != 0U) && (gap > g_usb_rx_diag.max_callback_gap_ms))
            g_usb_rx_diag.max_callback_gap_ms = gap;
        g_usb_rx_diag.callbacks_with_data++;
        g_usb_rx_diag.last_callback_tick = now;
        g_usb_rx_diag.raw_bytes += rx_len;
        diagnostic_ready_tick = now;
        if (rx_len > sizeof(usb_buffer)) g_usb_rx_diag.oversize_callback++;
        diagnostic_observe_rx(usb_buffer, (uint16_t)((rx_len > sizeof(usb_buffer)) ? sizeof(usb_buffer) : rx_len));
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
    uint32_t diagnostic_last_task_tick = 0U;
    uint32_t diagnostic_ring_wait_tick = 0U;
    uint8_t diagnostic_task_seen = 0U;
    uint8_t diagnostic_ring_wait_seen = 0U;
    ApplicationTypeDef diagnostic_last_state = APPLICATION_IDLE;

    for (;;)
    {
        thread_wake_time = osKernelSysTick();
        diagnostic_poll_timeout();
        {
            uint32_t now = HAL_GetTick();
            uint32_t elapsed = now - diagnostic_last_task_tick;
            if (diagnostic_task_seen && (elapsed > g_usb_rx_diag.max_task_gap_ms))
                g_usb_rx_diag.max_task_gap_ms = elapsed;
            diagnostic_task_seen = 1U;
            diagnostic_last_task_tick = now;
            if (usb_rx_pending) {
                elapsed = now - diagnostic_request_tick;
                if (elapsed > g_usb_rx_diag.max_pending_ms) g_usb_rx_diag.max_pending_ms = elapsed;
            }
            if (usb_rx_ready) {
                /* Read the callback timestamp before sampling the current tick.
                 * The loop's earlier 'now' may predate the receive callback. */
                uint32_t ready_tick = diagnostic_ready_tick;
                elapsed = HAL_GetTick() - ready_tick;
                if (elapsed > g_usb_rx_diag.max_ready_hold_ms) g_usb_rx_diag.max_ready_hold_ms = elapsed;
            }
            if (usb_rx_waiting_for_ring) {
                if (!diagnostic_ring_wait_seen) diagnostic_ring_wait_tick = now;
                diagnostic_ring_wait_seen = 1U;
                elapsed = now - diagnostic_ring_wait_tick;
                if (elapsed > g_usb_rx_diag.max_ring_wait_ms) g_usb_rx_diag.max_ring_wait_ms = elapsed;
            } else diagnostic_ring_wait_seen = 0U;
            if (Appli_state != diagnostic_last_state) {
                if (Appli_state == APPLICATION_READY) g_usb_rx_diag.ready_transitions++;
                if (Appli_state == APPLICATION_DISCONNECT) {
                    g_usb_rx_diag.disconnect_transitions++;
                    if (usb_rx_ready) g_usb_rx_diag.discarded_ready_packets++;
                    g_usb_rx_diag.discarded_partial_line_bytes += diagnostic_line_bytes;
                    diagnostic_prefix_len = 0U;
                    diagnostic_line_bytes = 0U;
                    diagnostic_previous_byte = 0U;
                    diagnostic_have_tick = 0U;
                    g_usb_rx_diag.line_bytes_pending = 0U;
                }
                diagnostic_last_state = Appli_state;
            }
        }
        if (Appli_state == APPLICATION_READY)
        {
            if (!log_host_session_active) {
                log_host_session_counter++;
                g_log_host_link_diag.session = HAL_GetTick() ^ (log_host_session_counter * 2654435761UL);
                log_host_session_active = 1U;
                g_log_host_ack_received = 0U;
                log_host_rx_armed = 0U;
                log_host_parser.used = 0U;
                log_host_last_ready_tick = HAL_GetTick() - 200U;
            }
            ring_ready = RecorderDataGetBufferStatus(&ring_free);
            usb_rx_ring_ready = ring_ready;
            usb_rx_ring_free = (uint32_t)ring_free;
            if ((g_usb_rx_diag.ring_samples == 0U) || (ring_free < g_usb_rx_diag.min_ring_free))
                g_usb_rx_diag.min_ring_free = (uint32_t)ring_free;
            g_usb_rx_diag.ring_samples++;

            if (usb_rx_ready)
            {
                if (!usb_rx_formatted_ready)
                {
                    if (usb_format_packet_with_timestamp(usb_rx_len, &usb_rx_timestamp_len,
                                                          &usb_rx_formatted_line_start))
                    {
                        usb_rx_formatted_ready = 1U;
                    }
                    else g_usb_rx_diag.format_fail++;
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
                    diagnostic_request_tick = HAL_GetTick();
                    g_usb_rx_timeout_diag.request_count++;
                    UsbRxTrace_Record(hhcd_USB_OTG_HS.Instance, cdc_handle->DataItf.InPipe,
                                      USB_RX_TRACE_APP_SUBMIT, g_usb_rx_timeout_diag.request_count);
                    /* 提交前先标记，避免完成回调清零后又被覆盖。 */
                    usb_rx_pending = 1U;
                    receive_status = USBH_CDC_Receive(&hUsbHostHS, usb_buffer, rx_request_len);
                    UsbRxTrace_Record(hhcd_USB_OTG_HS.Instance, cdc_handle->DataItf.InPipe,
                                      USB_RX_TRACE_APP_RETURN, (uint32_t)receive_status);
                    usb_rx_last_receive_status = (uint32_t)receive_status;
                    if (receive_status == USBH_OK) log_host_rx_armed = 1U;
                    if (receive_status != USBH_OK)
                    {
                        /* 提交失败，没有等待中的接收请求。 */
	                    usb_rx_pending = 0U;
                        usb_rx_receive_fail_count++;
                    }
                }
            }
            /* 接收请求已提交且日志文件可写，再发送 READY。
             * 提交前置 pending，防止快速完成回调清零后被覆盖。 */
            if (log_host_rx_armed && RecorderLogIsReady() && !g_log_host_ack_received &&
                !log_host_tx_pending && cdc_handle != NULL && cdc_handle->data_tx_state == CDC_IDLE &&
                (HAL_GetTick() - log_host_last_ready_tick >= 200U)) {
                USBH_StatusTypeDef tx_status;
                LogLinkBuild(log_host_ready_frame, LOG_LINK_READY, g_log_host_link_diag.session);
                log_host_tx_pending = 1U;
                log_host_last_ready_tick = HAL_GetTick();
                g_log_host_link_diag.ready_attempts++;
                tx_status = USBH_CDC_Transmit(&hUsbHostHS, log_host_ready_frame, sizeof(log_host_ready_frame));
                if (tx_status == USBH_OK) g_log_host_link_diag.ready_started++;
                else {
                    log_host_tx_pending = 0U;
                    g_log_host_link_diag.tx_failed++;
                }
            }
        }
        else
        {
            log_host_session_active = 0U;
            log_host_rx_armed = 0U;
            log_host_tx_pending = 0U;
            g_log_host_ack_received = 0U;
            log_host_parser.used = 0U;
            usb_rx_pending = 0U;
            usb_rx_ready = 0U;
            usb_rx_formatted_ready = 0U;
            usb_rx_line_start = 1U;
            usb_rx_waiting_for_ring = 0U;
        }
        osDelayUntil(&thread_wake_time, 1);
    }
}

