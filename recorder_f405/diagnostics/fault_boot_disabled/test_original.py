from pathlib import Path

base=Path(__file__).resolve().parent
test=base/'native_test';test.mkdir(exist_ok=True)
mock=r'''
#ifndef FAULT_MOCK_H
#define FAULT_MOCK_H
#include <stdint.h>
#include <stddef.h>
#define __attribute__(x)
#define FAULT_LOG_NATIVE_TEST 1
typedef struct {uint32_t RSR;} mock_rcc_t;
typedef struct {uint32_t CFSR,HFSR,DFSR,AFSR,BFAR,MMFAR,SHCSR,ICSR,VTOR,ABFSR,AIRCR;} mock_scb_t;
typedef struct {uint32_t FPCCR;} mock_fpu_t;
extern mock_rcc_t mock_rcc;
extern mock_scb_t mock_scb;
extern mock_fpu_t mock_fpu;
#define RCC (&mock_rcc)
#define SCB (&mock_scb)
#define FPU (&mock_fpu)
#define RCC_RSR_RMVF (1UL<<16)
#define RCC_RSR_PORRSTF (1UL<<23)
#define RCC_RSR_BORRSTF (1UL<<21)
#define SCB_CFSR_MSTKERR_Msk (1UL<<4)
#define SCB_CFSR_MUNSTKERR_Msk (1UL<<3)
#define SCB_CFSR_MLSPERR_Msk (1UL<<5)
#define SCB_CFSR_STKERR_Msk (1UL<<12)
#define SCB_CFSR_UNSTKERR_Msk (1UL<<11)
#define SCB_CFSR_LSPERR_Msk (1UL<<13)
#define SCB_AIRCR_VECTKEY_Pos 16U
#define SCB_AIRCR_PRIGROUP_Msk (7UL<<8)
#define SCB_AIRCR_SYSRESETREQ_Msk (1UL<<2)
#define __DSB() ((void)0)
#define __ISB() ((void)0)
#define SCB_SHCSR_USGFAULTENA_Msk (1UL<<18)
void FaultLog_NativeTestTrap(void);
void mock_watchdog_wait(void);
#define __NOP() mock_watchdog_wait()
uint32_t __get_IPSR(void);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t);
uint32_t osKernelSysTick(void);
#define USBD_OK 0
#define USBD_BUSY 1
#define USBD_FAIL 2
extern volatile uint8_t g_log_link_ready;
extern volatile uint32_t cdc_tx_pending_count,cdc_tx_inflight;
typedef struct {uint32_t session;} log_link_diag_t;
typedef struct {uint32_t deinit_count;} cdc_tx_diag_t;
extern volatile log_link_diag_t g_log_link_diag;
extern volatile cdc_tx_diag_t g_cdc_tx_diag;
#endif
'''
(test/'mock.h').write_text(mock)
for name in ['main.h','usbd_cdc_if.h','cmsis_os.h']:
    (test/name).write_text('#include "mock.h"\n')
source=(base/'staged/fault_log.c').read_text(encoding='utf-8')
program=r'''
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
uint32_t __get_IPSR(void){return 3;}
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
'''
program+='\n'+source
program+=r'''
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
int main(void)
{
    unsigned i;uint32_t address,regs[12];uint32_t *ram;
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
    assert(!fault_frame_address(0x2001F000,0xFFFFFFFD,0,&address));
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
    /* 自动注入：门限、握手、待报保护、热复位防循环、冷启动重新布防。 */
    memset((void*)&g_fault_log_diag,0,sizeof(g_fault_log_diag));
    mock_rcc.RSR=RCC_RSR_PORRSTF;FaultLog_BootInit();g_log_link_ready=1;
    assert(!fault_test_fired());
    FaultLog_TestService(9999);assert(trap_count==0);
    g_log_link_ready=0;FaultLog_TestService(10000);assert(trap_count==0);
    g_log_link_ready=1;g_fault_log_diag.pending=1;FaultLog_TestService(10000);assert(trap_count==0);
    g_fault_log_diag.pending=0;mock_scb.SHCSR=SCB_SHCSR_USGFAULTENA_Msk;
    FaultLog_TestService(10000);assert(trap_count==1 && fault_test_fired());
    assert(!(mock_scb.SHCSR&SCB_SHCSR_USGFAULTENA_Msk));
    FaultLog_TestService(20000);FaultLog_TestTrigger();assert(trap_count==1);
    warm_boot();FaultLog_TestService(20000);assert(trap_count==1);
    if(setjmp(reset_jump)==0)FaultLog_Capture(0,0,0xFFFFFFFD,regs);
    assert(fault_test_fired());warm_boot();assert(g_fault_log_diag.pending);
    g_fault_log_diag.pending=0;FaultLog_TestService(20000);assert(trap_count==1);
    memset((void*)&g_fault_log_diag,0,sizeof(g_fault_log_diag));
    mock_rcc.RSR=RCC_RSR_PORRSTF;FaultLog_BootInit();FaultLog_TestService(10000);assert(trap_count==2);

    /* 已经自动触发过的情况下，手动请求仍能重新倒计时。 */
    g_log_link_ready=0;g_fault_log_diag.pending=1;
    g_fault_test_request=1;FaultLog_TestService(50000);
    assert(g_fault_test_request==0 && g_fault_test_state==1 && g_fault_test_remaining_ms==10000);
    FaultLog_TestService(59999);assert(trap_count==2 && g_fault_test_remaining_ms==1);
    FaultLog_TestService(60000);assert(trap_count==3 && g_fault_test_state==2);
    FaultLog_TestService(70000);assert(trap_count==3);
    /* 再次写 1 重新计时，取消请求不会遗留自动触发。 */
    g_fault_test_request=1;FaultLog_TestService(71000);
    g_fault_test_request=1;FaultLog_TestService(75000);
    FaultLog_TestService(81000);assert(trap_count==3 && g_fault_test_remaining_ms==4000);
    g_fault_test_request=2;FaultLog_TestService(82000);
    assert(g_fault_test_state==0 && g_fault_test_request==0);
    FaultLog_TestService(100000);assert(trap_count==3);
    /* 无符号差值支持 tick 回绕。 */
    g_fault_test_request=1;FaultLog_TestService(0xFFFFFF00U);
    FaultLog_TestService(9743);assert(trap_count==3 && g_fault_test_remaining_ms==1);
    FaultLog_TestService(9744);assert(trap_count==4);
    g_fault_test_request=1;FaultLog_TestService(12000);assert(g_fault_test_state==1);
    warm_boot();assert(g_fault_test_request==0 && g_fault_test_state==0 && g_fault_test_remaining_ms==0);
    assert(fault_test_fired());

#else
    FaultLog_TestService(10000);assert(trap_count==0);
#endif

    puts("PASS: RAM manual request/rearm/cancel, reset clearing, offline/pending injection, wraparound; 10s injection, warm-reset latch, test-disabled guard; cold/warm reset, checksum rejection, actual capture and watchdog wait without software reset, safe/FP stack decoding, invalid-stack fallback, READY gate, BUSY retry, three-part replay, drain-before-clear, disconnect replay, repeated reboot retention");
    return 0;
}
'''
(test/'test_fault_log.c').write_text(program,encoding='utf-8')
header=base/'staged'
# 实际 log.h 用于验证 ERROR 入口签名与等级。
(test/'log.h').write_bytes(Path('D:/RM/rm_main_gimbal/User/Support/log.h').read_bytes())
(test/'run.cmd').write_text('@echo off\ncall "D:\\Application\\Visual Studio 2022\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\n'+f'cl /nologo /utf-8 /W4 /I"{header}" /Fe:test_fault_log.exe test_fault_log.c\n'+'if errorlevel 1 exit /b 1\ntest_fault_log.exe\n',encoding='utf-8')
print(test)
