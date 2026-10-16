
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "usb_task.h"
#define APPLICATION_READY 2U
#define USB_OTG_HOST_BASE 0x100U
#define USB_OTG_HOST_PORT_BASE 0x200U
#define USB_OTG_HOST_CHANNEL_BASE 0x300U
#define USB_OTG_HOST_CHANNEL_SIZE 0x40U
typedef struct { uint32_t GAHBCFG, GRSTCTL, GINTSTS, GINTMSK, HNPTXSTS, GRXFSIZ, DIEPTXF0_HNPTXFSIZ, HPTXFSIZ; } USB_OTG_GlobalTypeDef;
typedef struct { uint32_t HFNUM, HAINT, HAINTMSK, HPTXSTS; } USB_OTG_HostTypeDef;
typedef struct { uint32_t HCCHAR, HCINT, HCINTMSK, HCTSIZ, HCDMA; } USB_OTG_HostChannelTypeDef;
typedef struct { uint32_t state, urb_state, ErrCnt; uint8_t *xfer_buff;
    uint32_t xfer_len, xfer_count, toggle_in, data_pid; } USB_OTG_HCTypeDef;
typedef struct { uint32_t state, data_rx_state, data_tx_state;
    struct { uint32_t InPipe, InEp, InEpSize; } DataItf;
    uint8_t *pRxData; uint32_t RxDataLength; } CDC_HandleTypeDef;
static struct { void *pData; } active_class;
static struct { uint32_t gState, Timer; void *unused;
    struct { void *pData; } *pActiveClass; } hUsbHostHS;
static struct { USB_OTG_GlobalTypeDef *Instance;
    struct { uint32_t Host_channels; } Init; USB_OTG_HCTypeDef hc[12]; } hhcd_USB_OTG_HS;
static uint64_t hardware[0x1000U / 8U];
static CDC_HandleTypeDef fixture_cdc;
static volatile uint8_t usb_rx_pending, usb_rx_ready, usb_rx_waiting_for_ring;
static uint32_t usb_rx_callback_count, usb_rx_ring_ready, usb_rx_ring_free;
static uint32_t Appli_state, diagnostic_request_tick, diagnostic_timeout_request_id;
static uint32_t mock_now_tick, irq_mask, simulate_callback_on_lock;
volatile usb_rx_timeout_diag_t g_usb_rx_timeout_diag = {0};
volatile usb_rx_diag_t g_usb_rx_diag;
volatile log_host_link_diag_t g_log_host_link_diag;
volatile uint8_t g_log_host_ack_received;
static uint32_t HAL_GetTick(void) { return mock_now_tick; }
static uint32_t __get_PRIMASK(void) { return irq_mask; }
static void complete(void) {
    usb_rx_pending = 0;
    usb_rx_callback_count++;
    g_usb_rx_timeout_diag.callback_count++;
    g_usb_rx_timeout_diag.last_callback_tick = mock_now_tick;
}
static void __disable_irq(void) {
    if (simulate_callback_on_lock) { simulate_callback_on_lock = 0; complete(); }
    irq_mask = 1;
}
static void __set_PRIMASK(uint32_t value) { irq_mask = value; }
static void __DMB(void) { }
/* 本测试只验证超时状态机，底层环形记录由 usb_rx_trace 的集成测试验证。 */
static void reset_fixture(void) {
    memset((void *)&g_usb_rx_timeout_diag, 0, sizeof(g_usb_rx_timeout_diag));
    memset(hardware, 0, sizeof(hardware));
    memset(&fixture_cdc, 0, sizeof(fixture_cdc));
    memset(&hhcd_USB_OTG_HS, 0, sizeof(hhcd_USB_OTG_HS));
    active_class.pData = &fixture_cdc;
    hUsbHostHS.pActiveClass = (void *)&active_class;
    hUsbHostHS.gState = 11;
    hhcd_USB_OTG_HS.Instance = (void *)hardware;
    hhcd_USB_OTG_HS.Init.Host_channels = 12;
    fixture_cdc.DataItf.InPipe = 4;
    fixture_cdc.DataItf.InEp = 0x81;
    fixture_cdc.DataItf.InEpSize = 64;
    fixture_cdc.data_rx_state = 4;
    Appli_state = APPLICATION_READY;
    diagnostic_request_tick = diagnostic_timeout_request_id = 0;
    usb_rx_pending = usb_rx_ready = usb_rx_waiting_for_ring = 0;
    usb_rx_callback_count = mock_now_tick = irq_mask = simulate_callback_on_lock = 0;
    g_log_host_ack_received = 0;
}
static void submit(uint32_t tick) {
    mock_now_tick = diagnostic_request_tick = tick;
    g_usb_rx_timeout_diag.request_count++;
    usb_rx_pending = 1;
}

#include "usb_rx_trace.h"
#define USB_OTG_HCCHAR_EPDIR (1U << 15)
#define USB_OTG_HCCHAR_EPTYP_Pos 18U
#define USB_OTG_HCCHAR_EPTYP (3U << 18)
#define USB_OTG_HCCHAR_CHENA (1UL << 31)
#define USB_OTG_HCCHAR_CHDIS (1UL << 30)
#define __IO volatile
#define CLEAR_INTERRUPT_MASK UINT32_MAX
#define USBx_HC(i) ((USB_OTG_HostChannelTypeDef *)(USBx_BASE + USB_OTG_HOST_CHANNEL_BASE + (i) * USB_OTG_HOST_CHANNEL_SIZE))
#define USBx_HOST ((USB_OTG_HostTypeDef *)(USBx_BASE + USB_OTG_HOST_BASE))
typedef enum { HAL_OK, HAL_ERROR } HAL_StatusTypeDef;
static HAL_StatusTypeDef USB_DisableGlobalInt(USB_OTG_GlobalTypeDef *usb) { usb->GAHBCFG &= ~1U; return HAL_OK; }
static HAL_StatusTypeDef USB_EnableGlobalInt(USB_OTG_GlobalTypeDef *usb) { usb->GAHBCFG |= 1U; return HAL_OK; }
static HAL_StatusTypeDef USB_FlushTxFifo(USB_OTG_GlobalTypeDef *usb, uint32_t fifo) { (void)usb; (void)fifo; return HAL_OK; }
static HAL_StatusTypeDef USB_FlushRxFifo(USB_OTG_GlobalTypeDef *usb) { (void)usb; return HAL_OK; }
volatile usb_rx_trace_diag_t g_usb_rx_trace_diag;
volatile usb_stop_first_failure_t g_usb_stop_first_failure;
static struct { uint32_t CTRL, CYCCNT; } mock_dwt;
static struct { uint32_t DEMCR; } mock_core_debug;
#define DWT (&mock_dwt)
#define CoreDebug (&mock_core_debug)
#define CoreDebug_DEMCR_TRCENA_Msk (1UL << 24)
#define DWT_CTRL_CYCCNTENA_Msk 1U
#define DWT_CTRL_NOCYCCNT_Msk (1UL << 25)
static uint32_t SystemCoreClock = 168000000U;
static void __DSB(void) { }
static void __ISB(void) { }
static uint32_t simulated_ipsr;
static uint32_t __get_IPSR(void) { return simulated_ipsr; }
static USB_OTG_HostChannelTypeDef *trace_regs(void) {
    return (void *)((uint8_t *)hardware + USB_OTG_HOST_CHANNEL_BASE + 4 * USB_OTG_HOST_CHANNEL_SIZE);
}
static void trace_fixture(void) {
    reset_fixture();
    memset((void *)&g_usb_rx_trace_diag, 0, sizeof(g_usb_rx_trace_diag));
    memset((void *)&g_usb_stop_first_failure, 0, sizeof(g_usb_stop_first_failure));
    memset(&mock_dwt, 0, sizeof(mock_dwt));
    memset(&mock_core_debug, 0, sizeof(mock_core_debug));
    g_usb_rx_trace_diag.capacity = USB_RX_TRACE_CAPACITY;
    g_usb_rx_trace_diag.lifecycle_capacity = USB_RX_TRACE_LIFECYCLE_CAPACITY;
    trace_regs()->HCCHAR = 0xC0588840;
    simulated_ipsr = 0;
}
static uint32_t diagnostic_stop_cycle_valid(void)
{
    return ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) != 0U &&
            (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U &&
            (DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) == 0U) ? 1U : 0U;
}
static void diagnostic_stop_cycle_init(void)
{
    if ((DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) == 0U) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        __DSB();
        __ISB();
    }
}
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
}HAL_StatusTypeDef USB_StopHost(USB_OTG_GlobalTypeDef *USBx)
{
  HAL_StatusTypeDef ret = HAL_OK;
  uintptr_t USBx_BASE = (uintptr_t)USBx;
  __IO uint32_t count = 0U;
  uint32_t value;
  uint32_t i;

  UsbRxTrace_Record(USBx, USB_RX_TRACE_NO_CHANNEL, USB_RX_TRACE_STOP_ENTER, USB_RX_TRACE_CALLER());
  (void)USB_DisableGlobalInt(USBx);

  /* Flush USB FIFO */
  if (USB_FlushTxFifo(USBx, 0x10U) != HAL_OK) /* all Tx FIFOs */
  {
    ret = HAL_ERROR;
  }

  if (USB_FlushRxFifo(USBx) != HAL_OK)
  {
    ret = HAL_ERROR;
  }

  /* Flush out any leftover queued requests. */
  for (i = 0U; i <= 15U; i++)
  {
    value = USBx_HC(i)->HCCHAR;
    value |=  USB_OTG_HCCHAR_CHDIS;
    value &= ~USB_OTG_HCCHAR_CHENA;
    value &= ~USB_OTG_HCCHAR_EPDIR;
    UsbRxTrace_Record(USBx, i, USB_RX_TRACE_STOP_FLUSH_BEFORE, value);
    USBx_HC(i)->HCCHAR = value;
    UsbRxTrace_Record(USBx, i, USB_RX_TRACE_STOP_FLUSH_AFTER, value);
  }

  /* Halt all channels to put them into a known state. */
  for (i = 0U; i <= 15U; i++)
  {
    value = USBx_HC(i)->HCCHAR;
    value |= USB_OTG_HCCHAR_CHDIS;
    value |= USB_OTG_HCCHAR_CHENA;
    value &= ~USB_OTG_HCCHAR_EPDIR;
    UsbRxTrace_Record(USBx, i, USB_RX_TRACE_STOP_HALT_BEFORE, value);
    USBx_HC(i)->HCCHAR = value;
    UsbRxTrace_Record(USBx, i, USB_RX_TRACE_STOP_HALT_AFTER, value);

    UsbRxTrace_StopWaitBegin(USBx, i, count);

    do
    {
      count++;

      if (count > 1000U)
      {
        break;
      }
    } while ((USBx_HC(i)->HCCHAR & USB_OTG_HCCHAR_CHENA) == USB_OTG_HCCHAR_CHENA);
    UsbRxTrace_StopWait(USBx, i, count);
  }

  /* Clear any pending Host interrupts */
  USBx_HOST->HAINT = CLEAR_INTERRUPT_MASK;
  USBx->GINTSTS = CLEAR_INTERRUPT_MASK;

  UsbRxTrace_Record(USBx, USB_RX_TRACE_NO_CHANNEL, USB_RX_TRACE_STOP_EXIT, (uint32_t)ret);
  (void)USB_EnableGlobalInt(USBx);

  return ret;
}
int main(void) {
    uint32_t n, before;
    uint32_t found_wait;
    uint64_t saved_hw[sizeof(hardware) / sizeof(hardware[0])];
    usb_rx_trace_entry_t frozen_main[USB_RX_TRACE_CAPACITY];
    usb_stop_first_failure_t saved_failure;
    trace_fixture();
    trace_regs()->HCINT = 0x82;
    trace_regs()->HCINTMSK = 0x58F;
    trace_regs()->HCTSIZ = 0x80040;
    hhcd_USB_OTG_HS.hc[4].ErrCnt = 1;
    mock_now_tick = 123; simulated_ipsr = 93;
    memcpy(saved_hw, hardware, sizeof(hardware));
    UsbRxTrace_Record(hardware, 4, USB_RX_TRACE_IRQ_ENTER, 2);
    assert(g_usb_rx_trace_diag.entries[0].valid);
    assert(g_usb_rx_trace_diag.entries[0].hcint == 0x82);
    assert(g_usb_rx_trace_diag.entries[0].error_count == 1);
    assert(g_usb_rx_trace_diag.entries[0].ipsr == 93 && g_usb_rx_trace_diag.entries[0].tick == 123);
    assert(irq_mask == 0 && memcmp(hardware, saved_hw, sizeof(hardware)) == 0);
    for (n = 1; n < USB_RX_TRACE_CAPACITY + 9; n++)
        UsbRxTrace_Record(hardware, 4, USB_RX_TRACE_START_AFTER, n);
    assert(g_usb_rx_trace_diag.written == 73 && g_usb_rx_trace_diag.next_index == 9);
    assert(g_usb_rx_trace_diag.valid_count == 64 && g_usb_rx_trace_diag.overwritten == 9);
    assert(g_usb_rx_trace_diag.entries[8].sequence == 72);
    assert(g_usb_rx_trace_diag.entries[9].sequence == 9);
    puts("PASS: IRQ raw flags, read-only capture, ordered ring and overwrite count");

    UsbRxTrace_Freeze(77);
    assert(g_usb_rx_trace_diag.frozen && g_usb_rx_trace_diag.freeze_request_id == 77);
    assert(g_usb_rx_trace_diag.entries[9].event == USB_RX_TRACE_TIMEOUT);
    assert(g_usb_rx_trace_diag.entries[9].detail == 77);
    before = g_usb_rx_trace_diag.written;
    for (n = 0; n < 100; n++) UsbRxTrace_Record(hardware, 4, USB_RX_TRACE_IRQ_ENTER, n);
    UsbRxTrace_Freeze(88);
    assert(g_usb_rx_trace_diag.written == before && g_usb_rx_trace_diag.freeze_count == 1);
    assert(g_usb_rx_trace_diag.freeze_request_id == 77);
    UsbRxTrace_Resume(); UsbRxTrace_Resume();
    assert(!g_usb_rx_trace_diag.frozen && g_usb_rx_trace_diag.resume_count == 1);
    UsbRxTrace_Record(hardware, 4, USB_RX_TRACE_SUBMIT_BEFORE, 64);
    assert(g_usb_rx_trace_diag.written == before + 1);
    puts("PASS: freeze keeps fault evidence, repeated freeze idempotent, resume continues history");

    trace_fixture();
    UsbRxTrace_Record(hardware, 99, USB_RX_TRACE_IRQ_ENTER, 0);
    UsbRxTrace_Record(NULL, 4, USB_RX_TRACE_IRQ_ENTER, 0);
    UsbRxTrace_Record((void *)0x1234, 4, USB_RX_TRACE_IRQ_ENTER, 0);
    UsbRxTrace_Record(hardware, 4, 0, 0);
    UsbRxTrace_Record(hardware, 4, USB_RX_TRACE_EVENT_LIMIT, 0);
    trace_regs()->HCCHAR &= ~USB_OTG_HCCHAR_EPDIR;
    UsbRxTrace_Record(hardware, 4, USB_RX_TRACE_IRQ_ENTER, 0);
    trace_regs()->HCCHAR = USB_OTG_HCCHAR_EPDIR; /* 控制 IN 不记录。 */
    UsbRxTrace_Record(hardware, 4, USB_RX_TRACE_IRQ_ENTER, 0);
    assert(g_usb_rx_trace_diag.written == 0 && irq_mask == 0);
    UsbRxTrace_Record(hardware, USB_RX_TRACE_NO_CHANNEL, USB_RX_TRACE_DISCONNECT, 0);
    assert(g_usb_rx_trace_diag.written == 1 && g_usb_rx_trace_diag.entries[0].hcchar == 0);
    puts("PASS: instance/channel/type filters, invalid events, global disconnect event");

    trace_fixture(); irq_mask = 1;
    UsbRxTrace_Record(hardware, 4, USB_RX_TRACE_HALT_ENTER, 0);
    UsbRxTrace_Freeze(3); UsbRxTrace_Resume();
    assert(irq_mask == 1);
    g_usb_rx_trace_diag.written = UINT32_MAX;
    g_usb_rx_trace_diag.sequence_next = UINT32_MAX;
    UsbRxTrace_Record(hardware, 4, USB_RX_TRACE_HALT_EXIT, 0);
    assert(g_usb_rx_trace_diag.entries[2].valid && g_usb_rx_trace_diag.entries[2].sequence == UINT32_MAX);
    UsbRxTrace_Record(hardware, 4, USB_RX_TRACE_IRQ_EXIT, 0);
    assert(g_usb_rx_trace_diag.entries[3].valid && g_usb_rx_trace_diag.entries[3].sequence == 0);
    puts("PASS: nested IRQ mask preserved, sequence wrap keeps valid marker");

    trace_fixture(); submit(500); mock_now_tick = 1499; diagnostic_poll_timeout();
    assert(!g_usb_rx_trace_diag.frozen);
    mock_now_tick = 1500; diagnostic_poll_timeout();
    assert(g_usb_rx_trace_diag.frozen && g_usb_rx_timeout_diag.active);
    assert(g_usb_rx_trace_diag.freeze_request_id == g_usb_rx_timeout_diag.first.request_id);
    before = g_usb_rx_trace_diag.written;
    mock_now_tick = 9000; diagnostic_poll_timeout();
    assert(g_usb_rx_trace_diag.written == before);
    complete(); diagnostic_poll_timeout();
    assert(!g_usb_rx_trace_diag.frozen && !g_usb_rx_timeout_diag.active);
    submit(9001); mock_now_tick = 10001; diagnostic_poll_timeout();
    assert(g_usb_rx_trace_diag.frozen && g_usb_rx_trace_diag.freeze_count == 2);
    assert(g_usb_rx_timeout_diag.timeout_count == 2);
    puts("PASS: actual timeout integration freezes once and resumes on completion");

    trace_fixture(); submit(0); mock_now_tick = 1000;
    simulate_callback_on_lock = 1; diagnostic_poll_timeout();
    assert(!g_usb_rx_trace_diag.frozen && !g_usb_rx_timeout_diag.first.valid);
    puts("PASS: callback race cannot spuriously freeze events");

    trace_fixture();
    trace_regs()->HCCHAR = 0;
    UsbRxTrace_ChannelInit(hardware, 4, 0x81, 2);
    assert(g_usb_rx_trace_diag.tracked_in_mask == (1U << 4));
    assert(g_usb_rx_trace_diag.lifecycle[0].event == USB_RX_TRACE_INIT_BEFORE);
    assert(g_usb_rx_trace_diag.lifecycle[0].hcchar == 0);
    assert(g_usb_rx_trace_diag.lifecycle[0].detail == 0x281);
    UsbRxTrace_Freeze(77);
    before = g_usb_rx_trace_diag.written;
    memcpy(frozen_main, (const void *)g_usb_rx_trace_diag.entries, sizeof(frozen_main));
    UsbRxTrace_Record(hardware, USB_RX_TRACE_NO_CHANNEL, USB_RX_TRACE_DISCONNECT, 0);
    UsbRxTrace_Record(hardware, USB_RX_TRACE_NO_CHANNEL, USB_RX_TRACE_STOP_ENTER, 0x8000123);
    trace_regs()->HCCHAR = 0xC0580840; /* StopHost 已清 EPDIR，仍追踪这个通道。 */
    UsbRxTrace_Record(hardware, 4, USB_RX_TRACE_STOP_HALT_AFTER, 0xC0580840);
    UsbRxTrace_StopWait(hardware, 4, 1001);
    assert(g_usb_rx_trace_diag.lifecycle[g_usb_rx_trace_diag.lifecycle_next_index - 1].event == USB_RX_TRACE_STOP_WAIT_EXIT);
    assert(g_usb_rx_trace_diag.lifecycle[g_usb_rx_trace_diag.lifecycle_next_index - 1].detail == 1001);
    assert(g_usb_rx_trace_diag.stop_wait_exhausted_mask == (1U << 4));
    assert(g_usb_rx_trace_diag.stop_wait_enabled_mask == (1U << 4));
    ((USB_OTG_HostChannelTypeDef *)((uint8_t *)hardware + USB_OTG_HOST_CHANNEL_BASE +
        14 * USB_OTG_HOST_CHANNEL_SIZE))->HCCHAR = USB_OTG_HCCHAR_CHENA;
    UsbRxTrace_StopWait(hardware, 14, 1002); /* 软件配置 12 通道，硬件统计仍覆盖到 15。 */
    assert(g_usb_rx_trace_diag.stop_wait_exhausted_mask == ((1U << 4) | (1U << 14)));
    UsbRxTrace_Record(hardware, USB_RX_TRACE_NO_CHANNEL, USB_RX_TRACE_STOP_EXIT, 0);
    assert(g_usb_rx_trace_diag.stop_calls == 1 && g_usb_rx_trace_diag.last_stop_result == 0);
    UsbRxTrace_ChannelInit(hardware, 4, 0x81, 2);
    UsbRxTrace_Record(hardware, 4, USB_RX_TRACE_INIT_AFTER, 0);
    assert(g_usb_rx_trace_diag.written == before && g_usb_rx_trace_diag.frozen == 1);
    assert(memcmp(frozen_main, (const void *)g_usb_rx_trace_diag.entries, sizeof(frozen_main)) == 0);
    assert(g_usb_rx_trace_diag.event_count[USB_RX_TRACE_STOP_HALT_AFTER] == 1);
    puts("PASS: frozen main retained while disconnect/stop/init events continue; EPDIR clearing cannot hide channel");

    for (n = 0; n < USB_RX_TRACE_LIFECYCLE_CAPACITY + 5; n++)
        UsbRxTrace_Record(hardware, USB_RX_TRACE_NO_CHANNEL, USB_RX_TRACE_CONNECT, n);
    assert(g_usb_rx_trace_diag.lifecycle_valid_count == USB_RX_TRACE_LIFECYCLE_CAPACITY);
    assert(g_usb_rx_trace_diag.lifecycle_overwritten > 0);
    n = (g_usb_rx_trace_diag.lifecycle_next_index + USB_RX_TRACE_LIFECYCLE_CAPACITY - 1) % USB_RX_TRACE_LIFECYCLE_CAPACITY;
    assert(g_usb_rx_trace_diag.lifecycle[n].detail == USB_RX_TRACE_LIFECYCLE_CAPACITY + 4);
    assert(g_usb_rx_trace_diag.lifecycle[n].sequence == g_usb_rx_trace_diag.sequence_next - 1);
    UsbRxTrace_ChannelInit(hardware, 4, 0x01, 2);
    assert(g_usb_rx_trace_diag.tracked_in_mask == 0);
    before = g_usb_rx_trace_diag.lifecycle_written;
    trace_regs()->HCCHAR = 0;
    UsbRxTrace_Record(hardware, 4, USB_RX_TRACE_INIT_AFTER, 0);
    assert(g_usb_rx_trace_diag.lifecycle_written == before);
    puts("PASS: lifecycle ring wraps independently, shared sequence orders rings, reassigned OUT channel untracked");

    trace_fixture();
    UsbRxTrace_ChannelInit(hardware, 4, 0x81, 2);
    UsbRxTrace_Freeze(7);
    before = g_usb_rx_trace_diag.written;
    memcpy(frozen_main, (const void *)g_usb_rx_trace_diag.entries, sizeof(frozen_main));
    assert(USB_StopHost((void *)hardware) == HAL_OK);
    assert(g_usb_rx_trace_diag.stop_wait_seen_mask == 0xFFFF);
    assert(g_usb_rx_trace_diag.stop_wait_enabled_mask == 0xFFFF);
    assert(g_usb_rx_trace_diag.stop_wait_exhausted_mask == 0xFFFF);
    assert(g_usb_rx_trace_diag.last_stop_result == HAL_OK);
    assert(g_usb_rx_trace_diag.written == before && g_usb_rx_trace_diag.frozen);
    assert(memcmp(frozen_main, (const void *)g_usb_rx_trace_diag.entries, sizeof(frozen_main)) == 0);
    found_wait = 0;
    for (n = 0; n < g_usb_rx_trace_diag.lifecycle_valid_count; n++) {
        if (g_usb_rx_trace_diag.lifecycle[n].event == USB_RX_TRACE_STOP_WAIT_EXIT) {
            assert(g_usb_rx_trace_diag.lifecycle[n].channel == 4);
            assert(g_usb_rx_trace_diag.lifecycle[n].detail == 1005);
            found_wait++;
        }
    }
    assert(found_wait == 1);
    assert(g_usb_stop_first_failure.valid && g_usb_stop_first_failure.complete);
    assert(g_usb_stop_first_failure.channel == 4 && g_usb_stop_first_failure.failure_count == 1005);
    assert(g_usb_stop_first_failure.stage_valid_mask == 0x7F);
    assert(g_usb_stop_first_failure.wait_seen_mask == 0xFFFF);
    assert(g_usb_stop_first_failure.wait_iterations[0] == 1001);
    assert(g_usb_stop_first_failure.wait_iterations[4] == 1);
    assert(g_usb_stop_first_failure.stages[USB_STOP_STAGE_WAIT_EXIT].hcchar & USB_OTG_HCCHAR_CHENA);
    assert(g_usb_stop_first_failure.stop_result == HAL_OK);
    memcpy(&saved_failure, (const void *)&g_usb_stop_first_failure, sizeof(saved_failure));
    assert(USB_StopHost((void *)hardware) == HAL_OK);
    UsbRxTrace_ChannelInit(hardware, 4, 0x81, 2);
    UsbRxTrace_Record(hardware, USB_RX_TRACE_NO_CHANNEL, USB_RX_TRACE_CONNECT, 0);
    assert(memcmp(&saved_failure, (const void *)&g_usb_stop_first_failure, sizeof(saved_failure)) == 0);
    puts("PASS: actual StopHost preserves shared counter/return behavior and records all-channel wait exhaustion while frozen");
    puts("PASS: first real receive-channel failure keeps seven stages and per-channel counts across later StopHost/reconnect calls");

    trace_fixture();
    UsbRxTrace_ChannelInit(hardware, 4, 0x81, 2);
    mock_dwt.CYCCNT = 123456;
    UsbRxTrace_Record(hardware, USB_RX_TRACE_NO_CHANNEL, USB_RX_TRACE_STOP_ENTER, 0x8000123);
    assert(mock_dwt.CYCCNT == 123456 && g_usb_stop_first_failure.cycle_counter_enabled == 1);
    mock_now_tick = 10; mock_dwt.CYCCNT = 200;
    UsbRxTrace_StopWaitBegin(hardware, 4, 4);
    mock_now_tick = 11; mock_dwt.CYCCNT = 368; trace_regs()->HCCHAR = 0;
    UsbRxTrace_StopWait(hardware, 4, 5);
    UsbRxTrace_Record(hardware, USB_RX_TRACE_NO_CHANNEL, USB_RX_TRACE_STOP_EXIT, HAL_OK);
    assert(!g_usb_stop_first_failure.valid && g_usb_stop_first_failure.complete);
    assert(g_usb_stop_first_failure.wait_elapsed_cycles[4] == 168);
    assert(g_usb_stop_first_failure.wait_elapsed_ms[4] == 1);
    trace_regs()->HCCHAR = 0xC0588840;
    UsbRxTrace_Record(hardware, USB_RX_TRACE_NO_CHANNEL, USB_RX_TRACE_STOP_ENTER, 0x8000456);
    assert(!g_usb_stop_first_failure.complete && g_usb_stop_first_failure.stop_call == 2);
    mock_now_tick = UINT32_MAX - 1; mock_dwt.CYCCNT = UINT32_MAX - 9;
    UsbRxTrace_StopWaitBegin(hardware, 4, 4);
    mock_now_tick = 1; mock_dwt.CYCCNT = 158;
    UsbRxTrace_StopWait(hardware, 4, 1005);
    assert(g_usb_stop_first_failure.valid && !g_usb_stop_first_failure.complete);
    assert(g_usb_stop_first_failure.wait_elapsed_cycles[4] == 168);
    assert(g_usb_stop_first_failure.wait_elapsed_ms[4] == 3);
    assert(g_usb_stop_first_failure.wait_iterations[4] == 1001);
    UsbRxTrace_Record(hardware, USB_RX_TRACE_NO_CHANNEL, USB_RX_TRACE_STOP_EXIT, HAL_ERROR);
    assert(g_usb_stop_first_failure.complete && g_usb_stop_first_failure.stop_result == HAL_ERROR);
    assert(g_usb_stop_first_failure.core_clock_hz == 168000000);
    puts("PASS: successful candidate reusable, CYCCNT not reset, tick/cycle wrap measured, valid then complete publication");

    trace_fixture();
    mock_dwt.CTRL = DWT_CTRL_NOCYCCNT_Msk;
    UsbRxTrace_ChannelInit(hardware, 4, 0x81, 2);
    UsbRxTrace_Record(hardware, USB_RX_TRACE_NO_CHANNEL, USB_RX_TRACE_STOP_ENTER, 1);
    UsbRxTrace_StopWaitBegin(hardware, 4, 0);
    UsbRxTrace_StopWait(hardware, 4, 1001);
    assert(g_usb_stop_first_failure.valid && !g_usb_stop_first_failure.cycle_counter_enabled);
    assert(!(g_usb_stop_first_failure.wait_cycle_valid_mask & (1U << 4)));
    trace_fixture();
    UsbRxTrace_Record(hardware, USB_RX_TRACE_NO_CHANNEL, USB_RX_TRACE_STOP_ENTER, 1);
    UsbRxTrace_StopWaitBegin(hardware, 12, 0);
    UsbRxTrace_StopWait(hardware, 12, 1001);
    assert(!g_usb_stop_first_failure.valid);
    puts("PASS: unsupported cycle counter flagged, unused channel cannot claim first-failure latch");
    return 0;
}
