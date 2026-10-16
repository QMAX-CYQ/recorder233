"""测试实际环形记录和超时冻结函数，并核对底层原控制流程未改变。"""
from pathlib import Path
import ast
import re
import subprocess

here = Path(__file__).resolve().parent
root = here.parents[1]
source = (root / 'User/Task/usb_task.c').read_text(encoding='utf-8')

def extract(signature, text=source):
    start = text.index(signature + '\n{')
    opening = text.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

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
static uint32_t simulated_ipsr;
static uint32_t __get_IPSR(void) { return simulated_ipsr; }
static USB_OTG_HostChannelTypeDef *trace_regs(void) {
    return (void *)((uint8_t *)hardware + USB_OTG_HOST_CHANNEL_BASE + 4 * USB_OTG_HOST_CHANNEL_SIZE);
}
static void trace_fixture(void) {
    reset_fixture();
    memset((void *)&g_usb_rx_trace_diag, 0, sizeof(g_usb_rx_trace_diag));
    g_usb_rx_trace_diag.capacity = USB_RX_TRACE_CAPACITY;
    g_usb_rx_trace_diag.lifecycle_capacity = USB_RX_TRACE_LIFECYCLE_CAPACITY;
    trace_regs()->HCCHAR = 0xC0588840;
    simulated_ipsr = 0;
}
'''
tests = r'''
int main(void) {
    uint32_t n, before;
    uint32_t found_wait;
    uint64_t saved_hw[sizeof(hardware) / sizeof(hardware[0])];
    usb_rx_trace_entry_t frozen_main[USB_RX_TRACE_CAPACITY];
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
    g_usb_rx_trace_diag.sequence_next = UINT32_MAX;
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
    puts("PASS: actual StopHost preserves shared counter/return behavior and records all-channel wait exhaustion while frozen");
    return 0;
}
'''
functions = '\n'.join(extract(signature) for signature in [
    'void UsbRxTrace_Record(const void *instance, uint32_t channel, uint32_t event, uint32_t detail)',
    'void UsbRxTrace_ChannelInit(const void *instance, uint32_t channel, uint32_t epnum, uint32_t ep_type)',
    'void UsbRxTrace_StopWait(const void *instance, uint32_t channel, uint32_t count)',
    'void UsbRxTrace_Freeze(uint32_t request_id)',
    'void UsbRxTrace_Resume(void)',
    'static void diagnostic_capture_timeout(uint32_t request_id)',
    'static void diagnostic_poll_timeout(void)'])
ll_source = (root / 'Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_usb.c').read_text(encoding='utf-8')
# 唯一平台适配：模拟进程使用 64 位地址，硬件目标的地址类型仍保持原样。
stop_function = extract('HAL_StatusTypeDef USB_StopHost(USB_OTG_GlobalTypeDef *USBx)', ll_source)
stop_function = stop_function.replace('uint32_t USBx_BASE = (uint32_t)USBx;',
                                     'uintptr_t USBx_BASE = (uintptr_t)USBx;')
(here / 'trace_test.c').write_text(prelude + extra + functions + stop_function + tests, encoding='utf-8')

# 删除新增只读钩子后，HAL/LL 的原寄存器写入、条件及返回值须与备份一致。
for relative in ['Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_hcd.c',
                 'Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_usb.c',
                 'USB_HOST/Target/usbh_conf.c']:
    old = (here / 'before' / relative).read_text(encoding='utf-8')
    new = (root / relative).read_text(encoding='utf-8')
    new = re.sub(r'^.*UsbRxTrace_Record\([^\n]*\);\n', '', new, flags=re.M)
    new = re.sub(r'^.*UsbRxTrace_(?:ChannelInit|StopWait)\([^\n]*\);\n', '', new, flags=re.M)
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
