"""测试实际环形记录和超时冻结函数，并核对底层原控制流程未改变。"""
from pathlib import Path
import ast
import re
import subprocess

here = Path(__file__).resolve().parent
root = here.parents[1]
source = (root / 'User/Task/usb_task.c').read_text(encoding='utf-8')

def extract(signature):
    start = source.index(signature + '\n{')
    opening = source.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

# 复用超时测试的硬件模拟定义，不执行其脚本。
tree = ast.parse((here.parent / 'usb_rx_timeout/test_timeout.py').read_text(encoding='utf-8'))
prelude = next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign)
               and any(isinstance(t, ast.Name) and t.id == 'prelude' for t in n.targets))
prelude = re.sub(r'static void UsbRxTrace_Freeze\(uint32_t request_id\) \{[^\n]*\}\n', '', prelude)
prelude = re.sub(r'static void UsbRxTrace_Resume\(void\) \{[^\n]*\}\n', '', prelude)
prelude = re.sub(r'\bcdc\b', 'fixture_cdc', prelude)
extra = r'''
#include "usb_rx_trace.h"
#define USB_OTG_HCCHAR_EPDIR (1U << 15)
#define USB_OTG_HCCHAR_EPTYP_Pos 18U
#define USB_OTG_HCCHAR_EPTYP (3U << 18)
volatile usb_rx_trace_diag_t g_usb_rx_trace_diag;
static uint32_t simulated_ipsr;
static uint32_t __get_IPSR(void) { return simulated_ipsr; }
static USB_OTG_HostChannelTypeDef *trace_regs(void) {
    return (void *)((uint8_t *)hardware + USB_OTG_HOST_CHANNEL_BASE + 4 * USB_OTG_HOST_CHANNEL_SIZE);
}
static void trace_fixture(void) {
    reset_fixture();
    memset((void *)&g_usb_rx_trace_diag, 0, sizeof(g_usb_rx_trace_diag));
    g_usb_rx_trace_diag.capacity = USB_RX_TRACE_CAPACITY;
    trace_regs()->HCCHAR = 0xC0588840;
    simulated_ipsr = 0;
}
'''
tests = r'''
int main(void) {
    uint32_t n, before;
    uint64_t saved_hw[sizeof(hardware) / sizeof(hardware[0])];
    trace_fixture();
    trace_regs()->HCINT = 0x82;
    trace_regs()->HCINTMSK = 0x58F;
    trace_regs()->HCTSIZ = 0x80040;
    hhcd_USB_OTG_HS.hc[4].ErrCnt = 1;
    now_tick = 123; simulated_ipsr = 93;
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
    UsbRxTrace_Record(hardware, 4, USB_RX_TRACE_HALT_EXIT, 0);
    assert(g_usb_rx_trace_diag.entries[2].valid && g_usb_rx_trace_diag.entries[2].sequence == UINT32_MAX);
    UsbRxTrace_Record(hardware, 4, USB_RX_TRACE_IRQ_EXIT, 0);
    assert(g_usb_rx_trace_diag.entries[3].valid && g_usb_rx_trace_diag.entries[3].sequence == 0);
    puts("PASS: nested IRQ mask preserved, sequence wrap keeps valid marker");

    trace_fixture(); submit(500); now_tick = 1499; diagnostic_poll_timeout();
    assert(!g_usb_rx_trace_diag.frozen);
    now_tick = 1500; diagnostic_poll_timeout();
    assert(g_usb_rx_trace_diag.frozen && g_usb_rx_timeout_diag.active);
    assert(g_usb_rx_trace_diag.freeze_request_id == g_usb_rx_timeout_diag.first.request_id);
    before = g_usb_rx_trace_diag.written;
    now_tick = 9000; diagnostic_poll_timeout();
    assert(g_usb_rx_trace_diag.written == before);
    complete(); diagnostic_poll_timeout();
    assert(!g_usb_rx_trace_diag.frozen && !g_usb_rx_timeout_diag.active);
    submit(9001); now_tick = 10001; diagnostic_poll_timeout();
    assert(g_usb_rx_trace_diag.frozen && g_usb_rx_trace_diag.freeze_count == 2);
    assert(g_usb_rx_timeout_diag.timeout_count == 2);
    puts("PASS: actual timeout integration freezes once and resumes on completion");

    trace_fixture(); submit(0); now_tick = 1000;
    simulate_callback_on_lock = 1; diagnostic_poll_timeout();
    assert(!g_usb_rx_trace_diag.frozen && !g_usb_rx_timeout_diag.first.valid);
    puts("PASS: callback race cannot spuriously freeze events");
    return 0;
}
'''
functions = '\n'.join(extract(signature) for signature in [
    'void UsbRxTrace_Record(const void *instance, uint32_t channel, uint32_t event, uint32_t detail)',
    'void UsbRxTrace_Freeze(uint32_t request_id)',
    'void UsbRxTrace_Resume(void)',
    'static void diagnostic_capture_timeout(uint32_t request_id)',
    'static void diagnostic_poll_timeout(void)'])
(here / 'trace_test.c').write_text(prelude + extra + functions + tests, encoding='utf-8')

# 删除新增只读钩子后，HAL/LL 的原寄存器写入、条件及返回值须与备份一致。
for relative in ['Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_hcd.c',
                 'Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_usb.c',
                 'USB_HOST/Target/usbh_conf.c']:
    old = (here / 'before' / relative).read_text(encoding='utf-8')
    new = (root / relative).read_text(encoding='utf-8')
    new = re.sub(r'^.*UsbRxTrace_Record\([^\n]*\);\n', '', new, flags=re.M)
    new = re.sub(r'^#include "usb_rx_trace.h"\n', '', new, flags=re.M)
    new = re.sub(r'  uint32_t trace_irq = .*?;\n', '', new, flags=re.S)
    new = re.sub(r'\s*if \(trace_irq != 0U\) \{\s*\}', '', new)
    new = new.replace('  HAL_StatusTypeDef trace_status;\n', '')
    new = new.replace('trace_status = USB_HC_StartXfer', 'return USB_HC_StartXfer')
    new = new.replace('  return trace_status;\n', '')
    def tokens(s):
        s = re.sub(r'/\*.*?\*/|//[^\n]*', '', s, flags=re.S)
        return re.findall(r'[A-Za-z_][A-Za-z_0-9]*|\d+|[^\s]', s)
    assert tokens(old) == tokens(new), relative
print('PASS: original HCD, USB LL and Host callback control flow preserved after removing trace hooks')

cmd = ('@echo off\ncall "D:\\Application\\Visual Studio 2022\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\n'
       f'cl /nologo /utf-8 /W4 /I "{root / "User/Task"}" trace_test.c /Fe:trace_test.exe\n'
       'if errorlevel 1 exit /b 1\ntrace_test.exe\n')
(here / 'run_test.cmd').write_text(cmd, encoding='utf-8')
result = subprocess.run(['cmd', '/c', str(here / 'run_test.cmd')], cwd=here,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
output = result.stdout.decode('utf-8', errors='replace')
(here / 'test.log').write_text(output, encoding='utf-8')
print(output)
raise SystemExit(result.returncode)
