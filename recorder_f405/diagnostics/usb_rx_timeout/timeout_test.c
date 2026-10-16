
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
typedef struct { uint32_t GAHBCFG, GRSTCTL, GINTSTS, GINTMSK, HNPTXSTS; } USB_OTG_GlobalTypeDef;
typedef struct { uint32_t HFNUM, HAINT, HAINTMSK; } USB_OTG_HostTypeDef;
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
static uint32_t now_tick, irq_mask, simulate_callback_on_lock;
volatile usb_rx_timeout_diag_t g_usb_rx_timeout_diag = {0};
volatile usb_rx_diag_t g_usb_rx_diag;
volatile log_host_link_diag_t g_log_host_link_diag;
volatile uint8_t g_log_host_ack_received;
static uint32_t HAL_GetTick(void) { return now_tick; }
static uint32_t __get_PRIMASK(void) { return irq_mask; }
static void complete(void) {
    usb_rx_pending = 0;
    usb_rx_callback_count++;
    g_usb_rx_timeout_diag.callback_count++;
    g_usb_rx_timeout_diag.last_callback_tick = now_tick;
}
static void __disable_irq(void) {
    if (simulate_callback_on_lock) { simulate_callback_on_lock = 0; complete(); }
    irq_mask = 1;
}
static void __set_PRIMASK(uint32_t value) { irq_mask = value; }
static void __DMB(void) { }
/* 本测试只验证超时状态机，底层环形记录由 usb_rx_trace 的集成测试验证。 */
static void UsbRxTrace_Freeze(uint32_t request_id) { (void)request_id; }
static void UsbRxTrace_Resume(void) { }
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
    usb_rx_callback_count = now_tick = irq_mask = simulate_callback_on_lock = 0;
    g_log_host_ack_received = 0;
}
static void submit(uint32_t tick) {
    now_tick = diagnostic_request_tick = tick;
    g_usb_rx_timeout_diag.request_count++;
    usb_rx_pending = 1;
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
}

int main(void) {
    uint32_t n;
    uint64_t saved_hw[sizeof(hardware) / sizeof(hardware[0])];
    USB_OTG_HostChannelTypeDef *ch;
    reset_fixture(); submit(100);
    ch = (void *)((uint8_t *)hardware + USB_OTG_HOST_CHANNEL_BASE + 4 * USB_OTG_HOST_CHANNEL_SIZE);
    ch->HCCHAR = 0xC0588840; ch->HCTSIZ = 0x80040; ch->HCDMA = 0x2001D040;
    hhcd_USB_OTG_HS.Instance->GRSTCTL = 0x80000000;
    memcpy(saved_hw, hardware, sizeof(hardware));
    now_tick = 1099; diagnostic_poll_timeout();
    assert(g_usb_rx_timeout_diag.timeout_count == 0);
    now_tick = 1100; diagnostic_poll_timeout();
    assert(g_usb_rx_timeout_diag.timeout_count == 1 && g_usb_rx_timeout_diag.active == 1);
    assert(g_usb_rx_timeout_diag.first.valid && g_usb_rx_timeout_diag.first.pending_ms == 1000);
    assert(g_usb_rx_timeout_diag.first.hcchar == 0xC0588840);
    assert(g_usb_rx_timeout_diag.first.hcdma == 0x2001D040);
    assert(g_usb_rx_timeout_diag.first.data_rx_state == 4);
    assert(g_usb_rx_timeout_diag.first.ack_received == 0);
    assert(usb_rx_pending == 1 && fixture_cdc.data_rx_state == 4 && irq_mask == 0);
    assert(memcmp(saved_hw, hardware, sizeof(hardware)) == 0);
    for (now_tick = 1101; now_tick < 6000; now_tick++) diagnostic_poll_timeout();
    assert(g_usb_rx_timeout_diag.timeout_count == 1 && g_usb_rx_timeout_diag.current_pending_ms == 5899);
    complete(); diagnostic_poll_timeout();
    assert(!g_usb_rx_timeout_diag.active && g_usb_rx_timeout_diag.ended_count == 1);
    assert(g_usb_rx_timeout_diag.last_end_reason == 1);
    puts("PASS: threshold, frozen snapshot, one event per request, passive capture, callback end");

    for (n = 0; n < 6; n++) {
        submit(10000 + n * 2000); now_tick += 1000; diagnostic_poll_timeout();
        complete(); diagnostic_poll_timeout();
    }
    assert(g_usb_rx_timeout_diag.timeout_count == 7 && g_usb_rx_timeout_diag.history_next == 7);
    assert(g_usb_rx_timeout_diag.first.request_id == 1);
    assert(g_usb_rx_timeout_diag.history[2].event_id == 7);
    assert(g_usb_rx_timeout_diag.history[3].event_id == 4);
    puts("PASS: first preserved, recent four snapshots wrap correctly");

    reset_fixture(); submit(UINT32_MAX - 499U); now_tick = 500;
    diagnostic_poll_timeout(); assert(g_usb_rx_timeout_diag.first.pending_ms == 1000);
    Appli_state = 3; diagnostic_poll_timeout();
    assert(!g_usb_rx_timeout_diag.active && g_usb_rx_timeout_diag.last_end_reason == 2);
    puts("PASS: HAL tick wrap and disconnect end");

    reset_fixture(); submit(0); now_tick = 1000;
    simulate_callback_on_lock = 1; diagnostic_poll_timeout();
    assert(!g_usb_rx_timeout_diag.first.valid && g_usb_rx_timeout_diag.timeout_count == 0);
    assert(!usb_rx_pending && irq_mask == 0);
    puts("PASS: callback racing timeout capture does not produce a stale snapshot");

    reset_fixture(); submit(0); now_tick = 1000; fixture_cdc.DataItf.InPipe = 255;
    diagnostic_poll_timeout();
    assert(g_usb_rx_timeout_diag.first.cdc_valid && !g_usb_rx_timeout_diag.first.hc_valid);
    puts("PASS: invalid pipe cannot access a channel outside the array");

    reset_fixture(); submit(0); now_tick = 1000; irq_mask = 1;
    diagnostic_poll_timeout(); assert(irq_mask == 1);
    reset_fixture(); submit(0); now_tick = 1000; hUsbHostHS.pActiveClass = NULL;
    diagnostic_poll_timeout(); assert(!g_usb_rx_timeout_diag.timeout_count && irq_mask == 0);
    puts("PASS: IRQ mask preserved and missing class safely skipped");

    reset_fixture(); submit(0); now_tick = 1000; diagnostic_poll_timeout();
    complete(); submit(1001); diagnostic_poll_timeout();
    assert(!g_usb_rx_timeout_diag.active && g_usb_rx_timeout_diag.last_end_reason == 1);
    now_tick = 2001; diagnostic_poll_timeout();
    assert(g_usb_rx_timeout_diag.timeout_count == 2 && g_usb_rx_timeout_diag.active);
    puts("PASS: completion and re-submit between polls starts an independent timeout");
    return 0;
}
