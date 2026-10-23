"""执行实际恢复状态机/软件状态重建函数，硬件与 RTOS 接口由模拟替代。"""
from pathlib import Path
import subprocess

here = Path(__file__).resolve().parent
root = here.parents[1]
app = (root / 'User/Comm/usb_comm.c').read_text(encoding='utf-8')
core = (root / 'Middlewares/ST/STM32_USB_Host_Library/Core/Src/usbh_core.c').read_text(encoding='utf-8')

def function(text, signature):
    start = text.index(signature + '\n{')
    opening = text.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

prelude = r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "usb_comm.h"
#define USBH_OK 0U
#define USBH_FAIL 2U
#define USBH_BUSY 1U
#define USBH_PORT_EVENT 1U
#define HOST_IDLE 0U
#define ENUM_IDLE 0U
#define CMD_SEND 0U
#define CTRL_SETUP 0U
#define USBH_MPS_DEFAULT 64U
#define USBH_ADDRESS_DEFAULT 0U
#define USBH_SPEED_FULL 1U
#define USBH_MAX_PIPES_NBR 16U
#define USBH_MAX_DATA_BUFFER 64U
#define APPLICATION_READY 2U
#define APPLICATION_START 1U
#define USB_OTG_HOST_PORT_BASE 0x200U
#define USB_OTG_HOST_CHANNEL_BASE 0x300U
#define USB_OTG_HOST_CHANNEL_SIZE 0x40U
#define USB_OTG_HCCHAR_CHENA (1U << 31)
#define USB_OTG_HCCHAR_CHDIS (1U << 30)
#define USB_OTG_HPRT_PCSTS 1U
#define OTG_HS_IRQn 0
#define OTG_HS_EP1_IN_IRQn 1
#define OTG_HS_EP1_OUT_IRQn 2
#define USBH_memset memset
typedef uint32_t USBH_StatusTypeDef;
typedef uint32_t HAL_StatusTypeDef;
#define HAL_OK 0U
#define HAL_ERROR 1U
#define HAL_TIMEOUT 3U
#define URB_ERROR 4U
#define URB_STALL 5U
typedef struct { uint32_t unused; } USB_OTG_GlobalTypeDef;
typedef struct { uint32_t HCCHAR; } USB_OTG_HostChannelTypeDef;
typedef struct { uint32_t Host_channels; } HCD_InitTypeDef;
typedef struct { uint32_t urb_state; } HC;
typedef struct { USB_OTG_GlobalTypeDef *Instance; HCD_InitTypeDef Init; HC hc[12]; void *pData; } HCD_HandleTypeDef;
typedef struct { struct {uint32_t InPipe;} DataItf; } CDC_HandleTypeDef;
typedef struct USBH_HandleTypeDef USBH_HandleTypeDef;
typedef struct { void *pData; USBH_StatusTypeDef (*DeInit)(USBH_HandleTypeDef *); } USBH_ClassTypeDef;
struct USBH_HandleTypeDef {
    uint32_t Pipes[16], gState, EnumState, RequestState, Timer;
    struct { uint32_t PortEnabled,is_connected,is_disconnected,is_ReEnumerated,RstCnt,EnumCnt,address,speed;
        uint8_t Data[64],CfgDesc_Raw[8],DevDesc[8],CfgDesc[8]; } device;
    struct { uint32_t state,pipe_size,errorcount,pipe_in,pipe_out; } Control;
    USBH_ClassTypeDef *pActiveClass;
    void *pData, *thread, *os_event, *pClass[1]; uint32_t ClassNumber;
};
static uint64_t hw[0x1000/8];
#define USB_OTG_HS ((USB_OTG_GlobalTypeDef *)hw)
static USBH_HandleTypeDef hUsbHostHS;
static HCD_HandleTypeDef hhcd_USB_OTG_HS;
static USBH_ClassTypeDef cdc_class;
volatile usb_host_recovery_diag_t g_usb_host_recovery_diag;
volatile usb_rx_diag_t g_usb_rx_diag;
static uint8_t usb_rx_pending,usb_rx_ready,usb_rx_waiting_for_ring;
static uint16_t usb_rx_len;
static uint32_t usb_rx_callback_count,diagnostic_request_tick,Appli_state;
static uint8_t log_host_session_active,log_host_rx_armed,log_host_tx_pending;
volatile uint8_t g_log_host_ack_received;
static struct {uint32_t used;} log_host_parser;
static uint8_t usb_rx_formatted_ready,usb_rx_line_start,diagnostic_previous_byte,diagnostic_have_tick;
static size_t usb_rx_timestamp_len;
static uint32_t diagnostic_prefix_len,diagnostic_line_bytes;
static uint32_t mock_tick, irq_mask, ipsr, complete_on_lock;
static uint32_t irq_on[3], reset_held, clock_on, resets, deinit_calls, init_calls, starts, vbus_calls, wakeups;
static uint32_t allocations,frees,init_result,start_result,soft_result,vbus_result;
static uint32_t first_failure_sentinel;
static uint32_t HAL_GetTick(void) {return mock_tick;}
static uint32_t __get_IPSR(void) {return ipsr;}
static uint32_t __get_PRIMASK(void) {return irq_mask;}
static void __disable_irq(void) {if(complete_on_lock){usb_rx_pending=0;complete_on_lock=0;} irq_mask=1;}
static void __set_PRIMASK(uint32_t v) {irq_mask=v;}
static void __DMB(void) {}
static void __DSB(void) {}
static void __ISB(void) {}
static void osDelay(uint32_t ms) {assert(!irq_mask);mock_tick+=ms;}
static void HAL_NVIC_DisableIRQ(uint32_t n) {irq_on[n]=0;}
static void HAL_NVIC_ClearPendingIRQ(uint32_t n) {(void)n;assert(!irq_on[n]);}
static void enable_clock(void) {clock_on=1;}
static void force_reset(void) {
    assert(!irq_on[0]&&!irq_on[1]&&!irq_on[2]&&clock_on);
    reset_held=1;resets++;memset(hw,0,sizeof(hw));
}
static void release_reset(void) {assert(clock_on);reset_held=0;}
#define __HAL_RCC_USB_OTG_HS_CLK_ENABLE() enable_clock()
#define __HAL_RCC_USB_OTG_HS_FORCE_RESET() force_reset()
#define __HAL_RCC_USB_OTG_HS_RELEASE_RESET() release_reset()
static HAL_StatusTypeDef HAL_HCD_DeInit(HCD_HandleTypeDef *h) {assert(reset_held);(void)h;deinit_calls++;clock_on=0;return HAL_OK;}
static HAL_StatusTypeDef HAL_HCD_Init(HCD_HandleTypeDef *h) {
    assert(!reset_held&&clock_on&&h->Init.Host_channels==12&&h->pData==&hUsbHostHS);
    init_calls++;irq_on[0]=irq_on[1]=irq_on[2]=1;return init_result;
}
static HAL_StatusTypeDef HAL_HCD_Start(HCD_HandleTypeDef *h) {(void)h;starts++;return start_result;}
static uint32_t HAL_HCD_GetCurrentFrame(HCD_HandleTypeDef *h) {(void)h;return 0;}
static void USBH_LL_SetTimer(USBH_HandleTypeDef *p,uint32_t t) {p->Timer=t;}
static USBH_StatusTypeDef USBH_LL_DriverVBUS(USBH_HandleTypeDef *p,uint8_t state) {
    (void)p;assert(state==1);vbus_calls++;mock_tick+=200;return vbus_result;
}
static void USBH_OS_PutMessage(USBH_HandleTypeDef *p,uint32_t e,uint32_t t,uint32_t priority) {
    (void)e;(void)t;(void)priority;assert(p->os_event==(void*)0x2222&&!irq_mask);wakeups++;
}
static USBH_StatusTypeDef mock_class_deinit(USBH_HandleTypeDef *p) {
    assert(reset_held&&!irq_on[0]&&!irq_mask);
    if(soft_result!=USBH_OK)return soft_result;
    assert(p->pActiveClass->pData);free(p->pActiveClass->pData);p->pActiveClass->pData=NULL;frees++;return USBH_OK;
}
static void attach_class(void) {
    assert(!cdc_class.pData);cdc_class.pData=malloc(sizeof(CDC_HandleTypeDef));assert(cdc_class.pData);
    allocations++;((CDC_HandleTypeDef*)cdc_class.pData)->DataItf.InPipe=4;
    cdc_class.DeInit=mock_class_deinit;hUsbHostHS.pActiveClass=&cdc_class;
}
static void setup(void) {
    if(cdc_class.pData)free(cdc_class.pData);
    memset(&cdc_class,0,sizeof(cdc_class));memset(&hUsbHostHS,0,sizeof(hUsbHostHS));
    memset(&hhcd_USB_OTG_HS,0,sizeof(hhcd_USB_OTG_HS));memset(hw,0,sizeof(hw));
    memset((void*)&g_usb_host_recovery_diag,0,sizeof(g_usb_host_recovery_diag));
    memset((void*)&g_usb_rx_diag,0,sizeof(g_usb_rx_diag));
    hhcd_USB_OTG_HS.Instance=USB_OTG_HS;hhcd_USB_OTG_HS.Init.Host_channels=12;
    hhcd_USB_OTG_HS.pData=&hUsbHostHS;hUsbHostHS.pData=&hhcd_USB_OTG_HS;
    hUsbHostHS.thread=(void*)0x1111;hUsbHostHS.os_event=(void*)0x2222;
    hUsbHostHS.ClassNumber=1;hUsbHostHS.pClass[0]=&cdc_class;hUsbHostHS.gState=11;
    allocations=frees=resets=deinit_calls=init_calls=starts=vbus_calls=wakeups=0;
    init_result=start_result=soft_result=vbus_result=0;
    irq_mask=ipsr=complete_on_lock=0;irq_on[0]=irq_on[1]=irq_on[2]=1;
    clock_on=1;reset_held=0;mock_tick=0;first_failure_sentinel=0xAABBCCDD;
    usb_rx_pending=1;usb_rx_ready=usb_rx_waiting_for_ring=0;usb_rx_len=0;
    usb_rx_callback_count=7;diagnostic_request_tick=0;Appli_state=APPLICATION_READY;
    g_log_host_ack_received=1;diagnostic_line_bytes=3;
    log_host_session_active=log_host_rx_armed=log_host_tx_pending=1;
    log_host_parser.used=5;usb_rx_formatted_ready=1;usb_rx_timestamp_len=99;
    attach_class();
}
static USB_OTG_HostChannelTypeDef *regs(void) {return(void*)((uint8_t*)hw+USB_OTG_HOST_CHANNEL_BASE+4*USB_OTG_HOST_CHANNEL_SIZE);}
static USBH_StatusTypeDef DeInitStateMachine(USBH_HandleTypeDef *p);
'''

functions = '\n'.join([
    function(core, 'static USBH_StatusTypeDef DeInitStateMachine(USBH_HandleTypeDef *phost)'),
    function(core, 'USBH_StatusTypeDef USBH_ResetStateForRecovery(USBH_HandleTypeDef *phost)'),
    function(app, 'static void usb_recovery_reset_session(void)'),
    function(app, 'uint8_t USBH_UserRecoveryService(USBH_HandleTypeDef *phost)'),
    function(app, 'static void usb_recovery_request(uint32_t reason, uint32_t hcchar, uint32_t urb_state)'),
    function(app, 'static uint8_t usb_recovery_poll(void)'),
])
tests = r'''
int main(void) {
    uint32_t n;
    setup();regs()->HCCHAR=0x80588840;mock_tick=6000;
    assert(!usb_recovery_poll()&&!g_usb_host_recovery_diag.requests);
    regs()->HCCHAR=0xC0588840;mock_tick=1999;
    assert(!usb_recovery_poll());mock_tick=2000;
    assert(usb_recovery_poll()&&g_usb_host_recovery_diag.reason==1);
    assert(g_usb_host_recovery_diag.state==USB_HOST_RECOVERY_REQUESTED&&resets==0);
    assert(usb_recovery_poll()&&g_usb_host_recovery_diag.requests==1);
    ipsr=93;assert(!USBH_UserRecoveryService(&hUsbHostHS)&&!resets);ipsr=0;
    usb_rx_ready=1;usb_rx_len=64; /* 请求发出后可能赶上最后一个完成回调。 */
    assert(USBH_UserRecoveryService(&hUsbHostHS));
    assert(g_usb_host_recovery_diag.state==USB_HOST_RECOVERY_WAIT_LINK);
    assert(resets==1&&init_calls==1&&starts==1&&vbus_calls==1&&allocations==frees);
    assert(hUsbHostHS.thread==(void*)0x1111&&hUsbHostHS.os_event==(void*)0x2222);
    assert(hUsbHostHS.ClassNumber==1&&hUsbHostHS.pClass[0]==&cdc_class&&!hUsbHostHS.pActiveClass);
    assert(hUsbHostHS.gState==HOST_IDLE&&!hUsbHostHS.device.is_connected);
    assert(!usb_rx_pending&&!usb_rx_ready&&!g_log_host_ack_received&&!log_host_session_active);
    assert(g_usb_host_recovery_diag.discarded_ready_bytes==64);
    assert(!g_usb_host_recovery_diag.restored&&first_failure_sentinel==0xAABBCCDD);
    Appli_state=APPLICATION_READY;g_log_host_ack_received=1;
    assert(!usb_recovery_poll()&&!g_usb_host_recovery_diag.restored);
    usb_rx_callback_count++;assert(!usb_recovery_poll()&&g_usb_host_recovery_diag.restored==1);
    puts("PASS: normal idle ignored, exact trigger, Host-thread recovery, DMA reset before free, resource identity retained, handshake confirms restoration");

    setup();regs()->HCCHAR=0xC0588840;mock_tick=2000;complete_on_lock=1;
    assert(!usb_recovery_poll()&&!g_usb_host_recovery_diag.requests);
    setup();regs()->HCCHAR=0xC0588840;mock_tick=3000;usb_rx_waiting_for_ring=1;
    assert(!usb_recovery_poll());usb_rx_waiting_for_ring=0;usb_rx_ready=1;
    assert(!usb_recovery_poll());
    puts("PASS: completion race, buffered data and ring backpressure cannot trigger recovery");

    setup();regs()->HCCHAR=0x80588840;hhcd_USB_OTG_HS.hc[4].urb_state=URB_ERROR;mock_tick=2000;
    assert(usb_recovery_poll()&&g_usb_host_recovery_diag.reason==2);
    init_result=HAL_TIMEOUT;assert(USBH_UserRecoveryService(&hUsbHostHS));
    assert(g_usb_host_recovery_diag.state==USB_HOST_RECOVERY_FAILED);
    assert(g_usb_host_recovery_diag.last_status==0x30003&&!irq_on[0]&&!irq_on[1]&&!irq_on[2]);
    assert(!starts&&allocations==frees&&!reset_held);
    mock_tick=g_usb_host_recovery_diag.start_tick+4999;assert(usb_recovery_poll());
    assert(g_usb_host_recovery_diag.requests==1);
    mock_tick++;assert(usb_recovery_poll()&&g_usb_host_recovery_diag.reason==4);
    init_result=HAL_OK;assert(USBH_UserRecoveryService(&hUsbHostHS));
    assert(g_usb_host_recovery_diag.state==USB_HOST_RECOVERY_WAIT_LINK&&starts==1);
    puts("PASS: init failure safely gates IRQs and retries after cooldown without leaking class data");

    setup();regs()->HCCHAR=0xC0588840;mock_tick=2000;
    assert(usb_recovery_poll());soft_result=USBH_FAIL;assert(USBH_UserRecoveryService(&hUsbHostHS));
    assert(g_usb_host_recovery_diag.last_status==0x10002&&!reset_held&&cdc_class.pData);
    mock_tick+=5000;soft_result=USBH_OK;assert(usb_recovery_poll());
    assert(USBH_UserRecoveryService(&hUsbHostHS)&&allocations==frees);
    puts("PASS: class cleanup failure retains ownership for a later successful retry");

    setup();regs()->HCCHAR=0xC0588840;mock_tick=2000;assert(usb_recovery_poll());
    assert(USBH_UserRecoveryService(&hUsbHostHS));
    mock_tick=g_usb_host_recovery_diag.restart_tick+10000;
    assert(!usb_recovery_poll()); /* 没有设备时不持续重建。 */
    *(uint32_t*)((uint8_t*)hw+USB_OTG_HOST_PORT_BASE)=1;
    assert(usb_recovery_poll()&&g_usb_host_recovery_diag.reason==3);
    puts("PASS: enumeration timeout retries only when a device is physically connected");

    setup();regs()->HCCHAR=0xC0588840;mock_tick=500;diagnostic_request_tick=UINT32_MAX-1499;
    assert(usb_recovery_poll()&&g_usb_host_recovery_diag.reason==1);
    start_result=HAL_ERROR;assert(USBH_UserRecoveryService(&hUsbHostHS));
    assert(g_usb_host_recovery_diag.last_status==0x40001&&!irq_on[0]&&!reset_held);
    setup();regs()->HCCHAR=0xC0588840;mock_tick=2000;assert(usb_recovery_poll());
    vbus_result=USBH_FAIL;assert(USBH_UserRecoveryService(&hUsbHostHS));
    assert(g_usb_host_recovery_diag.last_status==0x50002&&!irq_on[0]&&!reset_held);
    puts("PASS: trigger tick wrap, HCD start and VBUS failure paths remain quiesced and report the failing phase");

    setup();
    for(n=0;n<100;n++) {
        mock_tick+=6000;diagnostic_request_tick=mock_tick-2000;
        usb_rx_pending=1;usb_rx_ready=usb_rx_waiting_for_ring=0;Appli_state=APPLICATION_READY;
        regs()->HCCHAR=0xC0588840;
        assert(usb_recovery_poll());assert(USBH_UserRecoveryService(&hUsbHostHS));
        assert(allocations==frees);
        assert(hUsbHostHS.thread==(void*)0x1111&&hUsbHostHS.os_event==(void*)0x2222);
        attach_class();Appli_state=APPLICATION_READY;g_log_host_ack_received=1;usb_rx_callback_count++;
        assert(!usb_recovery_poll());
    }
    assert(g_usb_host_recovery_diag.attempts==100&&g_usb_host_recovery_diag.restored==100);
    assert(first_failure_sentinel==0xAABBCCDD);
    free(cdc_class.pData);cdc_class.pData=NULL;frees++;assert(allocations==frees);
    puts("PASS: 100 recovery/enumeration cycles retain original queue/thread and balance every CDC allocation");
    return 0;
}
'''
(here / 'recovery_test.c').write_text(prelude + functions + tests, encoding='utf-8')
ll = root / 'Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_usb.c'
assert 'UsbRxTrace' not in ll.read_text()
assert app.index('if (usb_recovery_poll())') < app.index('    diagnostic_poll_timeout();')
assert core.count('if (USBH_UserRecoveryService((USBH_HandleTypeDef *)argument) == 0U)') == 2
print('PASS: HAL StopHost/LL trace hooks removed; recovery guard precedes diagnostic access; OS branches invoke serialized hook')
cmd = ('@echo off\ncall "D:\\Application\\Visual Studio 2022\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\n'
       f'cl /nologo /utf-8 /W4 /I "{root / "User/Comm"}" recovery_test.c /Fe:recovery_test.exe\n'
       'if errorlevel 1 exit /b 1\nrecovery_test.exe\n')
(here / 'run_test.cmd').write_text(cmd, encoding='utf-8')
result = subprocess.run(['cmd', '/c', str(here / 'run_test.cmd')], cwd=here,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
output = result.stdout.decode('utf-8', errors='replace')
(here / 'test.log').write_text(output, encoding='utf-8')
print(output)
raise SystemExit(result.returncode)
