"""抽取实际诊断函数，以模拟硬件验证超时边界和现场保留行为。"""
from pathlib import Path
import re
import subprocess

here = Path(__file__).resolve().parent
root = here.parent.parent
source = (root / 'User/Task/usb_task.c').read_text(encoding='utf-8')

def extract(signature):
    start = source.index(signature + '\n{')
    opening = source.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

prelude = r'''
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
static CDC_HandleTypeDef cdc;
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
    memset(&cdc, 0, sizeof(cdc));
    memset(&hhcd_USB_OTG_HS, 0, sizeof(hhcd_USB_OTG_HS));
    active_class.pData = &cdc;
    hUsbHostHS.pActiveClass = (void *)&active_class;
    hUsbHostHS.gState = 11;
    hhcd_USB_OTG_HS.Instance = (void *)hardware;
    hhcd_USB_OTG_HS.Init.Host_channels = 12;
    cdc.DataItf.InPipe = 4;
    cdc.DataItf.InEp = 0x81;
    cdc.DataItf.InEpSize = 64;
    cdc.data_rx_state = 4;
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
'''

tests = r'''
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
    assert(usb_rx_pending == 1 && cdc.data_rx_state == 4 && irq_mask == 0);
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

    reset_fixture(); submit(0); now_tick = 1000; cdc.DataItf.InPipe = 255;
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
'''

(here / 'timeout_test.c').write_text(re.sub(r'\bcdc\b', 'fixture_cdc', prelude) + '\n' +
    extract('static void diagnostic_capture_timeout(uint32_t request_id)') + '\n' +
    extract('static void diagnostic_poll_timeout(void)') + '\n' +
    re.sub(r'\bcdc\b', 'fixture_cdc', tests), encoding='utf-8')
cmd = ('@echo off\ncall "D:\\Application\\Visual Studio 2022\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\n'
       f'cl /nologo /utf-8 /W4 /I "{root / "User/Task"}" timeout_test.c /Fe:timeout_test.exe\n'
       'if errorlevel 1 exit /b 1\ntimeout_test.exe\n')
(here / 'run_test.cmd').write_text(cmd, encoding='utf-8')
result = subprocess.run(['cmd', '/c', str(here / 'run_test.cmd')], cwd=here,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
output = result.stdout.decode('utf-8', errors='replace')
(here / 'test.log').write_text(output, encoding='utf-8')
print(output)
raise SystemExit(result.returncode)
