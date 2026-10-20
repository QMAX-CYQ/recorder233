
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>
#include "mock.h"
#include "fault_log.h"
mock_rcc_t mock_rcc;mock_scb_t mock_scb;mock_fpu_t mock_fpu;
volatile uint8_t g_log_link_ready;
volatile uint32_t cdc_tx_pending_count,cdc_tx_inflight;
volatile log_link_diag_t g_log_link_diag;
volatile cdc_tx_diag_t g_cdc_tx_diag;
static uint8_t usb_result;
static unsigned sent;
static char lines[16][768];
static jmp_buf reset_jump;
static uint32_t irq_mask;
static unsigned trap_count;
void FaultLog_NativeTestTrap(void){trap_count++;}
void mock_watchdog_wait(void){assert(mock_scb.AIRCR==0);longjmp(reset_jump,1);}
static uint32_t mock_ipsr=3;
uint32_t __get_IPSR(void){return mock_ipsr;}
uint32_t __get_PRIMASK(void){return irq_mask;}
void __disable_irq(void){irq_mask=1;}
void __set_PRIMASK(uint32_t value){irq_mask=value;}
uint32_t osKernelSysTick(void){return 100;}
uint8_t sys_printf(uint8_t level,const char *tag,const char *fmt,...)
{
    va_list args;int len;
    assert(level==0 && strcmp(tag,"ERROR: ")==0);
    if(usb_result!=USBD_OK)return usb_result;
    assert(sent<16);va_start(args,fmt);len=vsnprintf(lines[sent],sizeof(lines[0]),fmt,args);va_end(args);
    assert(len>0 && len<768);sent++;cdc_tx_pending_count++;return USBD_OK;
}

#include "../staged/fault_log.c"
#include "../staged/nmi_log.c"
static void make_pending(void)
{
    fault_empty();g_fault_record.pending=1;g_fault_record.count=7;
    g_fault_record.exception=3;g_fault_record.frame[6]=0x08001234;
    g_fault_record.cfsr=0x02000000;g_fault_record.tick=12345;g_fault_record.tick_valid=1;
    fault_commit();
}
static void warm_boot(void)
{
    memset((void*)&g_fault_log_diag,0,sizeof(g_fault_log_diag));
    memset(&replay_record,0,sizeof(replay_record));replay_started=0;
    mock_rcc.RSR=0x01000000;FaultLog_BootInit();
}

static void test_nmi(void)
{
    unsigned i;
    uint32_t regs[12], saved[sizeof(fault_record_t)/4];
    uint32_t *ram=(uint32_t*)VirtualAlloc((void*)0x20010000,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(ram==(void*)0x20010000);
    assert(sizeof(nmi_record_t)==272 && sizeof(fault_record_t)==180);
    mock_rcc.RSR=RCC_RSR_PORRSTF;FaultLog_BootInit();
    assert(g_nmi_log_diag.cold_boot && !g_nmi_log_diag.invalid && !g_nmi_record.context.pending);
    make_pending();memcpy(saved,(const void*)&g_fault_record,sizeof(saved));
    for(i=0;i<12;i++)regs[i]=0xF0000000+i;
    for(i=0;i<8;i++)ram[0xF020/4+i]=0xA0000000+i;
    mock_rcc.CR=0x12345678;mock_rcc.CIFR=RCC_CIFR_HSECSSF|RCC_CIFR_LSECSSF;
    mock_rcc.CFGR=0x9ABCDEF0;mock_rcc.PLL1DIVR=0xABCDEFFF;
    mock_rcc.D2CCIP2R=0x12340000;mock_scb.CCR=0x10000;mock_scb.CFSR=0;
    mock_ipsr=2;g_fault_capture_active=1;
    if(setjmp(reset_jump)==0)NmiLog_Capture(0x2001F020,0,0xFFFFFFF1,regs);
    assert(!memcmp(saved,(const void*)&g_fault_record,sizeof(saved)));
    assert(g_nmi_record.context.magic==NMI_LOG_MAGIC && g_nmi_record.context.size==272);
    assert(g_nmi_record.context.checksum==nmi_checksum(&g_nmi_record));
    assert(g_nmi_record.context.exception==2 && g_nmi_record.context.stack_valid);
    assert(g_nmi_record.context.frame[6]==0xA0000006 && g_nmi_record.context.callee[7]==0xF0000007);
    assert(g_nmi_record.interrupted_capture==1 && mock_rcc.CIFR==(RCC_CIFR_HSECSSF|RCC_CIFR_LSECSSF));
    assert(g_nmi_record.rcc_cr==0x12345678 && g_nmi_record.rcc_pll1divr==0xABCDEFFF);
    warm_boot();assert(g_nmi_log_diag.pending && g_fault_log_diag.pending);
    assert(nmi_next_count==2 && g_nmi_log_diag.valid);
    sent=0;cdc_tx_pending_count=0;cdc_tx_inflight=0;usb_result=USBD_OK;g_log_link_ready=0;
    assert(FaultLog_Service() && !sent);
    g_log_link_ready=1;usb_result=USBD_BUSY;
    assert(FaultLog_Service() && !sent && g_nmi_log_diag.retries==1);
    usb_result=USBD_OK;assert(FaultLog_Service());
    assert(strstr(lines[0],"[nmi]") && strstr(lines[0],"hse_css=00000001") && strstr(lines[0],"lse_css=00000001"));
    g_log_link_diag.session++;g_cdc_tx_diag.deinit_count++;
    assert(FaultLog_Service() && g_nmi_log_diag.replay_restarts==1 && g_nmi_log_diag.part==1);
    assert(strstr(lines[1],"part=1/4"));
    for(i=0;i<3;i++)assert(FaultLog_Service());
    assert(sent==5 && g_nmi_log_diag.part==4 && g_nmi_record.context.pending);
    assert(strstr(lines[2],"pll1divr=ABCDEFFF") && strstr(lines[3],"pc=A0000006"));
    assert(FaultLog_Service() && sent==5); /* still queued */
    cdc_tx_pending_count=0;cdc_tx_inflight=1;assert(FaultLog_Service() && sent==5);
    cdc_tx_inflight=0;assert(FaultLog_Service()); /* NMI drained, start hardfault */
    assert(!g_nmi_record.context.pending && g_nmi_log_diag.reports==1 && sent==6);
    assert(strstr(lines[5],"[hardfault]") && g_fault_log_diag.part==1);
    assert(FaultLog_Service());assert(FaultLog_Service());
    cdc_tx_pending_count=0;assert(!FaultLog_Service());
    assert(!g_fault_record.pending && !g_nmi_log_diag.pending);
    warm_boot();assert(!g_nmi_log_diag.pending && !g_fault_log_diag.pending);
    mock_rcc.CIFR=0;mock_scb.CFSR=SCB_CFSR_STKERR_Msk;
    if(setjmp(reset_jump)==0)NmiLog_Capture(0,0,0xFFFFFFFD,regs);
    assert(!g_nmi_record.context.stack_valid && g_nmi_record.context.frame[6]==0);
    warm_boot();assert(g_nmi_log_diag.pending && nmi_replay.context.count==2);
    g_nmi_record.rcc_cr^=1;warm_boot();assert(g_nmi_log_diag.invalid && !g_nmi_log_diag.pending);
    assert(g_nmi_record.context.checksum==nmi_checksum(&g_nmi_record));
#if FAULT_LOG_ENABLE_TEST
    g_nmi_test_request=1;NmiLog_TestService();
    assert(g_nmi_test_request==0 && mock_scb.ICSR==SCB_ICSR_NMIPENDSET_Msk);
#endif
    assert(VirtualFree(ram,0,MEM_RELEASE));mock_ipsr=3;
    puts("PASS NMI: independent record/stack-frame, raw CSS/clock preservation, 4-part replay before hardfault, BUSY/reconnect/drain, warm retention, checksum rejection, invalid-frame fallback, software trigger");
}

int main(void)
{
    unsigned i;uint32_t address,regs[12];uint32_t *ram;
    uint32_t saved_record[sizeof(fault_record_t)/sizeof(uint32_t)];
    for(i=0;i<sizeof(saved_record)/sizeof(saved_record[0]);i++)
        ((volatile uint32_t*)&g_fault_record)[i]=0x13570000U+i;
    memcpy(saved_record,(const void*)&g_fault_record,sizeof(saved_record));
    fault_flush_record();
    assert(memcmp(saved_record,(const void*)&g_fault_record,sizeof(saved_record))==0);
    assert(fault_frame_address(0x2404F000UL-32U,0xFFFFFFFDUL,0,&address));
    assert(fault_frame_address(0x2404F000UL,0xFFFFFFFDUL,0,&address));
    assert(!fault_frame_address(0x2404FFF0UL,0xFFFFFFFDUL,0,&address));
    memset((void*)&g_fault_record,0xA5,sizeof(g_fault_record));
    mock_rcc.RSR=RCC_RSR_PORRSTF;FaultLog_BootInit();
    assert(fault_valid(&g_fault_record) && !g_fault_record.pending && g_fault_log_diag.cold_boot);
    make_pending();warm_boot();assert(g_fault_log_diag.pending && replay_record.count==7 && next_fault_count==8);
    assert(FaultLog_Service() && sent==0); /* 等待握手 */
    g_log_link_ready=1;usb_result=USBD_BUSY;
    assert(FaultLog_Service() && !g_fault_log_diag.part && g_fault_log_diag.retries==1);
    assert(g_fault_record.pending);
    usb_result=USBD_OK;
    for(i=0;i<3;i++)assert(FaultLog_Service());
    assert(sent==3 && g_fault_log_diag.part==3 && g_fault_record.pending);
    assert(strstr(lines[0],"part=1/3") && strstr(lines[0],"pc=08001234"));
    assert(strstr(lines[1],"part=2/3") && strstr(lines[2],"part=3/3"));
    assert(FaultLog_Service() && g_fault_record.pending); /* 入队不等于完成 */
    cdc_tx_pending_count=0;cdc_tx_inflight=1;assert(FaultLog_Service());
    cdc_tx_inflight=0;assert(!FaultLog_Service());
    assert(fault_valid(&g_fault_record) && !g_fault_record.pending && g_fault_log_diag.reports==1 && irq_mask==0);
    warm_boot();assert(!g_fault_log_diag.pending); /* 成功补报后的复位不再重复 */
    make_pending();warm_boot();assert(FaultLog_Service());
    g_log_link_ready=0;assert(FaultLog_Service() && g_fault_record.pending);
    g_log_link_ready=1;g_log_link_diag.session++;g_cdc_tx_diag.deinit_count++;
    assert(FaultLog_Service());assert(g_fault_log_diag.part==1 && g_fault_log_diag.replay_restarts==1);
    assert(strstr(lines[sent-1],"part=1/3"));
    /* 补报未完成又复位，记录仍可校验并再次补发。 */
    warm_boot();assert(g_fault_log_diag.pending && replay_record.count==7);
    g_fault_record.frame[6]^=1;warm_boot();assert(!g_fault_log_diag.pending && g_fault_log_diag.invalid==1);
    make_pending();mock_rcc.RSR=RCC_RSR_BORRSTF;FaultLog_BootInit();assert(!g_fault_record.pending);
    assert(fault_frame_address(0x24001000,0xFFFFFFFD,0,&address) && address==0x24001000);
    assert(fault_frame_address(0x20001000,0xFFFFFFED,0,&address) && address==0x20001000);
    assert(!fault_frame_address(0,0xFFFFFFFD,0,&address));
    assert(fault_frame_address(0x2001F000,0xFFFFFFFD,0,&address));
    assert(!fault_frame_address(0x24001001,0xFFFFFFFD,0,&address));
    assert(!fault_frame_address(0x24001000,0xFFFFFFFD,SCB_CFSR_STKERR_Msk,&address));
    assert(!fault_frame_address(0x2404FFF0,0xFFFFFFED,0,&address));
    assert(!fault_frame_address(0xFFFFFFF0,0xFFFFFFED,0,&address));
    assert(!fault_frame_address(0x24001000,0x12345678,0,&address));
    /* 将模拟异常帧放在真实可解引用的低地址 RAM，运行实际保存函数。 */
    ram=(uint32_t*)VirtualAlloc((void*)0x24000000,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(ram==(void*)0x24000000);
    for(i=0;i<8;i++)ram[0x1000/4+i]=0x100+i;
    for(i=0;i<12;i++)regs[i]=0x200+i;
    mock_scb.CFSR=0;FaultLog_Heartbeat(9876);
    if(setjmp(reset_jump)==0)FaultLog_Capture(0x24002000,0x24001000,0xFFFFFFFD,regs);
    assert(fault_valid(&g_fault_record) && g_fault_record.pending && g_fault_record.stack_valid);
    assert(g_fault_record.frame[6]==0x106 && g_fault_record.callee[7]==0x207);
    assert(g_fault_record.tick==9876 && g_fault_record.tick_valid && g_fault_record.primask==0x208);
    warm_boot();assert(g_fault_log_diag.pending && replay_record.frame[6]==0x106);
    /* 扩展浮点帧的核心寄存器仍位于 SP+0，不应偏移 18 个字。 */
    for(i=8;i<26;i++)ram[0x1000/4+i]=0xBAD00000+i;
    if(setjmp(reset_jump)==0)FaultLog_Capture(0x24002000,0x24001000,0xFFFFFFED,regs);
    assert(g_fault_record.stack_valid && g_fault_record.frame[6]==0x106);
    mock_scb.CFSR=SCB_CFSR_STKERR_Msk;
    if(setjmp(reset_jump)==0)FaultLog_Capture(0,0,0xFFFFFFFD,regs);
    assert(fault_valid(&g_fault_record) && !g_fault_record.stack_valid && !g_fault_record.frame[6]);
    assert(VirtualFree(ram,0,MEM_RELEASE));

#if FAULT_LOG_ENABLE_TEST
    /* Automatic timer starts only after link/replay readiness, and repeats after warm reset. */
    memset((void*)&g_fault_log_diag,0,sizeof(g_fault_log_diag));
    mock_rcc.RSR=RCC_RSR_PORRSTF;FaultLog_BootInit();
    g_log_link_ready=0;FaultLog_TestService(50000);assert(trap_count==0 && !g_fault_test_state);
    g_log_link_ready=1;g_fault_log_diag.pending=1;FaultLog_TestService(50000);assert(trap_count==0);
    g_fault_log_diag.pending=0;FaultLog_TestService(50000);assert(g_fault_test_state==3);
    FaultLog_TestService(59999);assert(trap_count==0 && g_fault_test_remaining_ms==1);
    FaultLog_TestService(60000);assert(trap_count==1 && g_fault_test_state==2 && fault_test_fired());
    FaultLog_TestService(90000);assert(trap_count==1);
    for(i=0;i<20;i++) {
        if(setjmp(reset_jump)==0)FaultLog_Capture(0,0,0xFFFFFFFD,regs);
        warm_boot();assert(g_fault_log_diag.pending && fault_test_fired());
        g_log_link_ready=1;sent=0;usb_result=USBD_OK;cdc_tx_pending_count=0;cdc_tx_inflight=0;
        FaultLog_TestService(20000);assert(trap_count==i+1 && g_fault_test_state==0);
        assert(FaultLog_Service());assert(FaultLog_Service());assert(FaultLog_Service());
        assert(sent==3 && g_fault_log_diag.pending);
        cdc_tx_pending_count=0;assert(!FaultLog_Service());
        assert(!g_fault_log_diag.pending && g_fault_log_diag.reports==1);
        FaultLog_TestService(30000);assert(g_fault_test_state==3 && g_fault_test_remaining_ms==10000);
        FaultLog_TestService(39999);assert(trap_count==i+1 && g_fault_test_remaining_ms==1);
        FaultLog_TestService(40000);assert(trap_count==i+2 && g_fault_test_state==2);
    }
    /* Manual request remains available offline and while replay is pending. */
    g_log_link_ready=0;g_fault_log_diag.pending=1;
    g_fault_test_request=1;FaultLog_TestService(50000);
    assert(g_fault_test_state==1 && g_fault_test_remaining_ms==10000);
    FaultLog_TestService(59999);assert(trap_count==21);
    FaultLog_TestService(60000);assert(trap_count==22);
    g_fault_test_request=1;FaultLog_TestService(71000);
    g_fault_test_request=2;FaultLog_TestService(72000);assert(g_fault_test_state==0);
    FaultLog_TestService(90000);assert(trap_count==22);
    /* Link interruption restarts the auto timer; unsigned subtraction handles wraparound. */
    g_log_link_ready=1;g_fault_log_diag.pending=0;FaultLog_TestService(100000);
    FaultLog_TestService(105000);assert(g_fault_test_remaining_ms==5000);
    g_log_link_ready=0;FaultLog_TestService(106000);assert(g_fault_test_state==0);
    g_log_link_ready=1;FaultLog_TestService(0xFFFFFF00U);
    FaultLog_TestService(9743);assert(trap_count==22 && g_fault_test_remaining_ms==1);
    FaultLog_TestService(9744);assert(trap_count==23);

#else
    FaultLog_TestService(10000);assert(trap_count==0);
#endif

    puts("PASS: 20 repeated warm-reset/capture/three-part-replay/10s-rearm cycles; offline/pending gate, manual requests, disconnect restart, tick wraparound; cold/warm reset, checksum rejection, actual capture and watchdog wait without software reset, safe/FP stack decoding, invalid-stack fallback, READY gate, BUSY retry, three-part replay, drain-before-clear, disconnect replay, repeated reboot retention");
    test_nmi();
    return 0;
}
