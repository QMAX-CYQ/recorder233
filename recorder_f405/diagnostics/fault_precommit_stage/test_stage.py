from pathlib import Path
import ast

base=Path(__file__).resolve().parent
prior=ast.parse((base.parent/'fault_boot_log/test_boot.py').read_text(encoding='utf-8'))
strings={t.id:ast.literal_eval(n.value) for n in prior.body if isinstance(n,ast.Assign) and isinstance(n.value,ast.Constant) and isinstance(n.value.value,str) for t in n.targets if isinstance(t,ast.Name)}
old=ast.parse(Path('D:/RM/rm_main_gimbal/diagnostics/fault_manual/test_fault_log.py').read_text(encoding='utf-8'))
mock=next(ast.literal_eval(n.value) for n in old.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='mock' for t in n.targets))
test=base/'native';test.mkdir(exist_ok=True)
(test/'mock.h').write_text(mock,encoding='utf-8')
for name in ('main.h','usbd_cdc_if.h','cmsis_os.h'):
    (test/name).write_text('#include "mock.h"\n',encoding='utf-8')
(test/'fault_log.h').write_bytes((base/'staged/fault_log.h').read_bytes())
(test/'log.h').write_bytes(Path('D:/RM/rm_main_gimbal/User/Support/log.h').read_bytes())
pre=strings['pre'].replace('#include <stdlib.h>','#include <stdlib.h>\n#include <setjmp.h>')
pre=pre.replace('void mock_watchdog_wait(void){abort();}', '''static jmp_buf reset_jump;
static unsigned capture_running, stop_stage, observed_mask;
void mock_watchdog_wait(void){if(capture_running)longjmp(reset_jump,1);abort();}
static void stage_barrier(void);
#undef __DSB
#define __DSB() stage_barrier()''')
tests=r'''
static void stage_barrier(void){
    unsigned stage=g_fault_capture_trace.stage_word & 255U;
    if(capture_running && stage>=1 && stage<=2){
        observed_mask|=1U<<(stage-1);
        if(stop_stage==stage)longjmp(reset_jump,1);
    }
}
static void test_stages(void){
    static const uint32_t regs[12]={0};
    unsigned target;
    for(target=1;target<=2;target++){
        reset_runtime();fault_empty();
        mock_scb.CFSR=SCB_CFSR_STKERR_Msk; /* 跳过宿主不存在的目标地址。 */
        g_fault_log_diag.last_tick=12345;next_fault_count=9;
        observed_mask=0;stop_stage=target;capture_running=1;
        if(setjmp(reset_jump)==0)FaultLog_Capture(0,0,0xFFFFFFFD,regs);
        capture_running=0;
        assert(observed_mask==((1U<<target)-1U));
        assert((g_fault_capture_trace.stage_word & 255U)==target);
        assert(g_fault_capture_trace.count==9 && g_fault_capture_trace.tick==12345);
        if(target==2 || target==3){assert(g_fault_record.pending && !g_fault_record.magic && !g_fault_record.checksum);}
        if(target==4){assert(!g_fault_record.magic && g_fault_record.checksum==fault_checksum(&g_fault_record));}
        if(target==5){assert(fault_valid(&g_fault_record));}
        mock_rcc.RSR=0;FaultLog_BootInit();
        assert(g_fault_boot_diag.capture_trace_valid && g_fault_boot_diag.capture_stage==target);
        assert(g_fault_boot_diag.capture_mask==((1U<<target)-1U));
        assert(g_fault_boot_diag.capture_count==9 && g_fault_boot_diag.capture_tick==12345);
        assert(!g_fault_capture_trace.stage_word); /* 下次启动快照完成后重新布防。 */
        boot_report();
        {char expected[48];snprintf(expected,sizeof(expected),"capture_stage=%u",target);assert(strstr(lines[1],expected));}
    }
    /* 完整捕获直至等待看门狗，同样经过两个提交前阶段。 */
    reset_runtime();fault_empty();stop_stage=0;observed_mask=0;capture_running=1;
    if(setjmp(reset_jump)==0)FaultLog_Capture(0,0,0xFFFFFFFD,regs);
    capture_running=0;assert(observed_mask==3 && fault_valid(&g_fault_record));
    /* 普通清空和补报提交不能把阶段重写成5。 */
    fault_capture_mark(2);fault_empty();assert((g_fault_capture_trace.stage_word&255)==2);
    fault_commit();assert((g_fault_capture_trace.stage_word&255)==2);
    /* 错误反码不解析；冷启动忽略看似有效的旧阶段；无故障新启动不沿用旧阶段。 */
    reset_runtime();fault_empty();g_fault_capture_trace.stage_word=0x46530005;
    FaultLog_BootInit();assert(!g_fault_boot_diag.capture_trace_valid && !g_fault_boot_diag.capture_stage);
    fault_capture_mark(5);mock_rcc.RSR=RCC_RSR_PORRSTF;FaultLog_BootInit();
    assert(!g_fault_boot_diag.capture_trace_valid && !g_fault_boot_diag.capture_word);
    mock_rcc.RSR=0;FaultLog_BootInit();assert(!g_fault_boot_diag.capture_trace_valid);
    puts("PASS: interruption at each of two precommit stages, retained snapshot/rearm, integrity, cold boot, normal commit isolation, actual log formatting");
}
'''
post=strings['post'].replace('int main(void){','static void test_stages(void);\nint main(void){').replace('    uint32_t expected;','    uint32_t expected;\n    test_stages();',1)
(test/'test_stage.c').write_text(pre+'\n'+(base/'staged/fault_log.c').read_text(encoding='utf-8')+post+tests,encoding='utf-8')
(test/'run.cmd').write_text('@echo off\ncall "D:\\Application\\Visual Studio 2022\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\ncl /nologo /utf-8 /W4 /wd4312 /Fe:test_stage.exe test_stage.c\nif errorlevel 1 exit /b 1\ntest_stage.exe\n',encoding='utf-8')
print(test)
