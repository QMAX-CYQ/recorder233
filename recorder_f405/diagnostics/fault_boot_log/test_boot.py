from pathlib import Path
import ast

base=Path(__file__).resolve().parent
old=ast.parse(Path('D:/RM/rm_main_gimbal/diagnostics/fault_manual/test_fault_log.py').read_text(encoding='utf-8'))
mock=next(ast.literal_eval(n.value) for n in old.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='mock' for t in n.targets))
test=base/'native';test.mkdir(exist_ok=True)
(test/'mock.h').write_text(mock,encoding='utf-8')
for name in ('main.h','usbd_cdc_if.h','cmsis_os.h'):
    (test/name).write_text('#include "mock.h"\n',encoding='utf-8')
(test/'fault_log.h').write_bytes((base/'staged/fault_log.h').read_bytes())
(test/'log.h').write_bytes(Path('D:/RM/rm_main_gimbal/User/Support/log.h').read_bytes())
pre=r'''
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "mock.h"
mock_rcc_t mock_rcc; mock_scb_t mock_scb; mock_fpu_t mock_fpu;
volatile uint8_t g_log_link_ready;
volatile uint32_t cdc_tx_pending_count, cdc_tx_inflight;
volatile log_link_diag_t g_log_link_diag;
volatile cdc_tx_diag_t g_cdc_tx_diag;
static uint32_t mask_value;
static uint8_t usb_result;
static unsigned sent;
static char lines[32][768];
uint32_t __get_IPSR(void){return 3;}
uint32_t __get_PRIMASK(void){return mask_value;}
void __disable_irq(void){mask_value=1;}
void __set_PRIMASK(uint32_t x){mask_value=x;}
uint32_t osKernelSysTick(void){return 100;}
void FaultLog_NativeTestTrap(void){}
void mock_watchdog_wait(void){abort();}
uint8_t sys_printf(uint8_t level,const char *tag,const char *fmt,...){
    int n; va_list args;
    assert((level==0 && !strcmp(tag,"ERROR: ")) || (level==1 && !strcmp(tag,"WARN: ")));
    if(usb_result!=USBD_OK)return usb_result;
    assert(sent<32); va_start(args,fmt); n=vsnprintf(lines[sent],768,fmt,args);va_end(args);
    assert(n>0 && n<768);sent++;cdc_tx_pending_count++;return USBD_OK;
}
'''
post=r'''
static void reset_runtime(void){
    memset((void*)&g_fault_log_diag,0,sizeof(g_fault_log_diag));
    memset((void*)&g_fault_boot_diag,0,sizeof(g_fault_boot_diag));
    memset((void*)&g_log_link_diag,0,sizeof(g_log_link_diag));
    memset((void*)&g_cdc_tx_diag,0,sizeof(g_cdc_tx_diag));
    replay_started=0;boot_report_started=0;sent=0;usb_result=USBD_OK;
    cdc_tx_pending_count=0;cdc_tx_inflight=0;g_log_link_ready=0;mock_rcc.RSR=0;
}
static void boot_report(void){
    g_log_link_ready=1;
    assert(FaultLog_Service());assert(FaultLog_Service());
    assert(g_fault_boot_diag.part==2 && sent==2);
    assert(FaultLog_Service() && g_fault_boot_diag.pending);
    cdc_tx_pending_count=0;cdc_tx_inflight=0;
}
int main(void){
    uint32_t expected;
    /* 冷启动即使 RAM 内有看似有效的旧记录，也不复制读取原记录。 */
    reset_runtime();fault_empty();g_fault_record.pending=1;fault_commit();
    mock_rcc.RSR=RCC_RSR_PORRSTF;FaultLog_BootInit();
    assert(g_fault_boot_diag.cold_boot && !g_fault_boot_diag.raw_valid);
    assert(!g_fault_boot_diag.raw.magic && !g_fault_boot_diag.calculated_checksum);
    assert(g_fault_log_diag.cold_boot && !g_fault_log_diag.pending);
    assert(FaultLog_Service() && !sent);boot_report();
    assert(!FaultLog_Service() && g_fault_boot_diag.reports==1);
    assert(strstr(lines[0],"cold=1 raw_valid=0"));

    /* 捕获实际观察到的半提交记录：清空后快照和计算结果仍可输出。 */
    reset_runtime();fault_empty();g_fault_record.pending=1;g_fault_record.count=7;
    g_fault_record.exception=3;g_fault_record.frame[6]=0x08001234;
    g_fault_record.magic=0;g_fault_record.checksum=0;expected=fault_checksum(&g_fault_record);
    FaultLog_BootInit();assert(g_fault_log_diag.invalid==1 && !g_fault_record.pending);
    assert(g_fault_boot_diag.raw.pending==1 && g_fault_boot_diag.raw.magic==0);
    assert(g_fault_boot_diag.raw.frame[6]==0x08001234);
    assert(g_fault_boot_diag.check_failed==17 && g_fault_boot_diag.calculated_checksum==expected);
    g_log_link_ready=1;usb_result=USBD_BUSY;
    assert(FaultLog_Service() && !sent && !g_fault_boot_diag.part && g_fault_boot_diag.retries==1);
    usb_result=USBD_OK;boot_report();assert(!FaultLog_Service());
    assert(strstr(lines[0],"check_failed=00000011") && strstr(lines[0],"pending=1 count=7"));
    assert(strstr(lines[1],"pc=08001234") && strstr(lines[1],"boot_invalid=1"));

    /* 全部失败位均按独立条件计算，不短路遗漏 checksum。 */
    reset_runtime();memset((void*)&g_fault_record,0,sizeof(g_fault_record));g_fault_record.pending=2;
    FaultLog_BootInit();assert(g_fault_boot_diag.check_failed==31);

    /* 有效记录保持完整三段补报；启动两段优先且不会清除待报记录。 */
    reset_runtime();fault_empty();g_fault_record.pending=1;g_fault_record.exception=3;fault_commit();
    expected=g_fault_record.checksum;FaultLog_BootInit();
    assert(g_fault_log_diag.valid && g_fault_log_diag.pending && !g_fault_boot_diag.check_failed);
    assert(g_fault_boot_diag.raw.checksum==expected);boot_report();
    assert(FaultLog_Service() && g_fault_boot_diag.reports==1 && g_fault_log_diag.part==1);
    assert(FaultLog_Service());assert(FaultLog_Service());
    assert(g_fault_log_diag.part==3 && sent==5 && strstr(lines[2],"part=1/3"));
    assert(FaultLog_Service() && !g_fault_log_diag.reports);
    cdc_tx_pending_count=0;assert(!FaultLog_Service());
    assert(g_fault_log_diag.reports==1 && !g_fault_record.pending && fault_valid(&g_fault_record));

    /* 两段发送中换会话、USB DeInit 均重发；直到新会话队列排空才结束。 */
    reset_runtime();fault_empty();FaultLog_BootInit();g_log_link_ready=1;
    assert(FaultLog_Service() && g_fault_boot_diag.part==1);
    g_log_link_diag.session++;
    assert(FaultLog_Service() && g_fault_boot_diag.part==1 && g_fault_boot_diag.replay_restarts==1);
    g_cdc_tx_diag.deinit_count++;
    assert(FaultLog_Service() && g_fault_boot_diag.part==1 && g_fault_boot_diag.replay_restarts==2);
    assert(FaultLog_Service());cdc_tx_pending_count=0;cdc_tx_inflight=1;
    assert(FaultLog_Service() && !g_fault_boot_diag.reports);
    cdc_tx_inflight=0;assert(!FaultLog_Service() && g_fault_boot_diag.reports==1 && !mask_value);
    puts("PASS: cold ECC-read avoidance, raw snapshot before clear, independent failure bits, checksum, offline/BUSY retry, two-part output, session/deinit replay, drain gate, intact HardFault replay");
    return 0;
}
'''
(test/'test_boot.c').write_text(pre+'\n'+(base/'staged/fault_log.c').read_text(encoding='utf-8')+post,encoding='utf-8')
(test/'run.cmd').write_text('@echo off\ncall "D:\\Application\\Visual Studio 2022\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\ncl /nologo /utf-8 /W4 /wd4312 /Fe:test_boot.exe test_boot.c\nif errorlevel 1 exit /b 1\ntest_boot.exe\n',encoding='utf-8')
print(test)
