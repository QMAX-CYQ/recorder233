from pathlib import Path
import hashlib, json

root=Path(__file__).resolve().parent
stage=root/'stress_test'
stage.mkdir(exist_ok=True)
sender=Path('D:/RM/rm_main_gimbal')
hashes={}
def read(name):
    b=(sender/name).read_bytes();hashes[name]=hashlib.sha256(b).hexdigest()
    try:s=b.decode('utf-8-sig')
    except UnicodeDecodeError:s=b.decode('gbk')
    return s.replace('\r\n','\n')
def save(name,s):
    p=stage/name;p.parent.mkdir(parents=True,exist_ok=True)
    p.write_bytes(s.replace('\n','\r\n').encode('utf-8'))

old=read('User/Task/debug_task.c')
externs=old[old.index('extern osThreadId CommTaskHandle;'):old.index('static uint32_t debug_stack_words')]
rcc='CR CFGR D1CFGR D2CFGR D3CFGR PLLCKSELR PLLCFGR PLL1DIVR PLL1FRACR PLL2DIVR PLL2FRACR PLL3DIVR PLL3FRACR D1CCIPR D2CCIP1R D2CCIP2R D3CCIPR AHB3ENR AHB1ENR AHB2ENR AHB4ENR APB3ENR APB1LENR APB1HENR APB2ENR APB4ENR RSR AHB3RSTR AHB1RSTR AHB2RSTR AHB4RSTR APB3RSTR'.split()
can='CCCR NBTP DBTP IR IE ILS ILE GFC RXF0S RXF1S TXFQS TXBRP TXBTO TXBCF TXBTIE TXBCIE'.split()
canextra='CREL ENDN TEST RWD TSCC TSCV TOCC TOCV TDCR SIDFC XIDFC XIDAM HPMS NDAT1 NDAT2 TXEFS'.split()
uart='ISR CR1 CR2 CR3 BRR PRESC RTOR GTPR'.split()
channels={}
channels['sys']=['RCC->'+x for x in rcc]
tasks='Comm ModeSwitch Status Chassis Gimbal Shoot UI Debug BoardComm'.split()
channels['task']=['debug_stack_words('+x+'TaskHandle)' for x in tasks]+[
    'uxTaskGetNumberOfTasks()', 'xPortGetFreeHeapSize()', 'xPortGetMinimumEverFreeHeapSize()',
    'g_debug_task_diag.loops','g_debug_task_diag.max_gap_ms','g_debug_task_diag.late_count',
    'g_debug_task_diag.catchup_count','g_debug_task_diag.max_run_ms',
    'cdc_tx_pending_count','cdc_tx_inflight','cdc_tx_queue_overflow_count',
    'g_cdc_tx_diag.accepted','g_cdc_tx_diag.completed','g_cdc_tx_diag.started',
    'g_cdc_tx_diag.accepted_bytes','g_cdc_tx_diag.completed_bytes','g_cdc_tx_diag.buffered',
    'g_cdc_tx_diag.queue_high_water','g_cdc_tx_diag.start_failed',
    'g_log_tx_diag.format_truncated','g_log_tx_diag.format_error',
    'g_log_tx_diag.last_line_bytes','g_log_tx_diag.max_line_bytes']
channels['fdcan1']=['can32['+str(i)+']' for i in range(32)]
channels['fdcan23']=['can2['+str(i)+']' for i in range(16)]+['can3['+str(i)+']' for i in range(16)]
for name,handles in [('uart123',['huart1','huart2','huart3']),('uart6910',['huart6','huart9','huart10'])]:
    channels[name]=[f'{h}.Instance->{field}' for h in handles for field in uart]+[f'{h}.{field}' for h in handles for field in ('gState','RxState')]+['g_log_tx_diag.accepted','g_log_tx_diag.busy']
irqs='FDCAN1_IT0 FDCAN1_IT1 FDCAN2_IT0 FDCAN3_IT0 USART1 USART2 USART6 OTG_HS'.split()
channels['nvic']=[f'NVIC->{field}[{bank}]' for bank in range(5) for field in ('ISER','ISPR','IABR')]+[f'NVIC_GetPriority({x}_IRQn)' for x in irqs]+[f'SCB->SHPR[{i}]' for i in range(9)]
djimotors=['driver_motor[0]','fric_motor[0]','fric_motor[1]','yaw_motor']
channels['dji']=[f'{m}.{field}' for m in djimotors for field in ('online','state','ecd','speed_rpm','rx_current','tx_current','send_cnt','receive_cnt')]
dmfields='online err_state send_cnt receive_cnt last_rx_tick p v kp kd t position velocity torque'.split()
channels['dm']=[f'debug_float_bits({m}.{field})' if field in ('p','v','kp','kd','t','position','velocity','torque') else f'{m}.{field}' for m in ('pit_motor','trigger_motor_2325') for field in dmfields]+[f'debug_float_bits(gimbal.{field})' for field in ('yaw_angle.ref','yaw_angle.fdb','pit_angle.ref','pit_angle.fdb','yaw_output','pit_output')]
imufields='pit yaw rol wx wy wz ax ay az'.split()
channels['imu']=[f'debug_float_bits({m}.{field})' for m in ('gimbal_imu','chassis_imu') for field in imufields]+['gimbal_imu.online','gimbal_imu.last_rx_tick','chassis_imu.online','chassis_imu.last_rx_tick']+[f'debug_float_bits(gimbal.{field})' for field in ('yaw_angle.ref','yaw_angle.fdb','yaw_spd.ref','yaw_spd.fdb','pit_angle.ref','pit_angle.fdb','pit_spd.ref','pit_spd.fdb','yaw_output','pit_output')]
assert all(len(x)==32 for x in channels.values()),{k:len(v) for k,v in channels.items()}
common=['SCB->ICSR','SCB->SHCSR','SCB->CFSR','SCB->HFSR','SCB->DFSR','SCB->VTOR','SCB->AIRCR','SCB->CCR','SCB->CPACR','CoreDebug->DEMCR','DWT->CTRL','DWT->CYCCNT','DWT->CPICNT','DWT->EXCCNT','DWT->LSUCNT','DWT->FOLDCNT']
c='''#include "debug_task.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "fdcan.h"
#include "usart.h"
#include "log.h"
#include "drv_dji_motor.h"
#include "drv_dm_motor.h"
#include "gimbal_task.h"
#include "prot_imu.h"
#include "usbd_cdc_if.h"
#include <string.h>

'''+externs+'''
volatile debug_task_diag_t g_debug_task_diag;

/* 每次只读取观测数据，不改变外设或内核配置。
 * 不读取 UART RDR、FDCAN PSR/ECR、SysTick CTRL 等有读副作用的寄存器。 */
static uint32_t debug_stack_words(osThreadId handle)
{
    return handle ? (uint32_t)uxTaskGetStackHighWaterMark((TaskHandle_t)handle) : 0U;
}

/* 浮点数据发送 IEEE-754 原始位模式，避免浮点格式化和数值转换溢出。 */
static uint32_t debug_float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/* 读取状态和配置寄存器，不操作 FIFO 确认寄存器或发送请求寄存器。 */
static void debug_fdcan_snapshot(FDCAN_GlobalTypeDef *can, uint32_t *words, uint32_t count)
{
    if (can == NULL) {
        memset(words, 0, count * sizeof(*words));
        return;
    }
'''
for i,field in enumerate(can):c+=f'    words[{i}] = can->{field};\n'
c+='    if (count > 16U) {\n'
for i,field in enumerate(canextra,16):c+=f'        words[{i}] = can->{field};\n'
c+='    }\n}\n\n'
c+='''/* 快照不是跨字段原子事务，业务任务和中断可以在采样时更新数据。 */
static void debug_snapshot(uint8_t channel, uint32_t *w)
{
    switch (channel) {
'''
for idx,(tag,fields) in enumerate(channels.items()):
    c+=f'    case {idx}U: /* {tag}：索引定义见压力测试说明。 */\n'
    if tag.startswith('fdcan'):
        c+='    {\n'
        if tag=='fdcan1':c+='        uint32_t can32[32];\n        debug_fdcan_snapshot(hfdcan1.Instance, can32, 32U);\n'
        else:c+='        uint32_t can2[16], can3[16];\n        debug_fdcan_snapshot(hfdcan2.Instance, can2, 16U);\n        debug_fdcan_snapshot(hfdcan3.Instance, can3, 16U);\n'
    for i,field in enumerate(fields):c+=f'        w[{i}] = (uint32_t)({field});\n'
    if tag.startswith('fdcan'):c+='    }\n'
    c+='        break;\n'
c+='    default:\n        memset(w, 0, 32U * sizeof(*w));\n        break;\n    }\n'
for i,field in enumerate(common,32):c+=f'    w[{i}] = {field};\n'
c+='}\n\n'
fmt='[%s] tick=%lu seq=%lu ch=%u n=48 w='+'/'.join(['%08lX']*48)+'\\r\\n'
c+='''/* 保留 tick 在行首，兼容 F405 的跨 USB 包行解析。
 * 一条日志包含 48 个固定宽度数据字，无填充字符串。 */
static void debug_send_snapshot(uint8_t channel, uint32_t tick, uint32_t sequence)
{
    static const char *const tags[DEBUG_LOG_CHANNEL_COUNT] = {
        "sys", "task", "fdcan1", "fdcan23", "uart123", "uart6910", "nvic", "dji", "dm", "imu"
    };
    uint32_t words[48];
    debug_snapshot(channel, words);
'''
c+=f'    DEBUG_INFO("{fmt}",\n        tags[channel], (unsigned long)tick, (unsigned long)sequence, (unsigned int)channel,\n'
c+=',\n'.join('        '+', '.join(f'(unsigned long)words[{i}]' for i in range(j,j+4)) for j in range(0,48,4))+');\n}\n\n'
c+='''void debug_task(void const *argument)
{
    uint32_t wake_time = osKernelSysTick();
    uint8_t channel = 0U;
    (void)argument;

    for (;;) {
        uint32_t tick = osKernelSysTick();
        uint32_t start_ms = HAL_GetTick();
        uint32_t run_ms;
        CDC_TxService_HS();
        if (g_debug_task_diag.loops != 0U) {
            uint32_t gap = tick - g_debug_task_diag.last_tick;
            if (gap > g_debug_task_diag.max_gap_ms) g_debug_task_diag.max_gap_ms = gap;
            if (gap > DEBUG_LOG_PERIOD_MS) g_debug_task_diag.late_count++;
            if (gap < DEBUG_LOG_PERIOD_MS) g_debug_task_diag.catchup_count++;
        }
        g_debug_task_diag.last_tick = tick;
        g_debug_task_diag.group_count[channel]++;
        debug_send_snapshot(channel, tick, g_debug_task_diag.loops);
        g_debug_task_diag.loops++;
        run_ms = HAL_GetTick() - start_ms;
        g_debug_task_diag.last_run_ms = run_ms;
        if (run_ms > g_debug_task_diag.max_run_ms) g_debug_task_diag.max_run_ms = run_ms;
        if (run_ms >= DEBUG_LOG_PERIOD_MS) g_debug_task_diag.overrun_count++;
        /* 每 10ms 一条，10 通道轮转；单通道周期为 100ms。 */
        channel = (uint8_t)((channel + 1U) % DEBUG_LOG_CHANNEL_COUNT);
        osDelayUntil(&wake_time, DEBUG_LOG_PERIOD_MS);
    }
}
'''
save('User/Task/debug_task.c',c)
read('User/Task/debug_task.h')
save('User/Task/debug_task.h','''#ifndef __DEBUG_TASK_H
#define __DEBUG_TASK_H

#include <stdint.h>

#define DEBUG_LOG_CHANNEL_COUNT 10U
#define DEBUG_LOG_PERIOD_MS 10U

/* 仅供调试器观察，不额外发送诊断报文。 */
typedef struct {
    uint32_t loops, last_tick, max_gap_ms, late_count, catchup_count;
    uint32_t group_count[DEBUG_LOG_CHANNEL_COUNT];
    uint32_t last_run_ms, max_run_ms, overrun_count;
} debug_task_diag_t;
extern volatile debug_task_diag_t g_debug_task_diag;
void debug_task(void const *argument);

#endif
''')
s=read('User/Support/log.h').replace('    uint32_t last_accepted_tick, max_accepted_gap_ms;',
    '    uint32_t last_accepted_tick, max_accepted_gap_ms;\n    uint32_t last_line_bytes, max_line_bytes;')
s=s.replace('void sys_printf(uint8_t level,', '/* 768 字节缓冲覆盖压力测试行的最坏长度，包含字符串结束符。 */\n#define LOG_FORMAT_BUFFER_SIZE 768U\n\nvoid sys_printf(uint8_t level,',1)
save('User/Support/log.h',s)
s=read('User/Support/log.c').replace('char buf[256 + 32];','char buf[LOG_FORMAT_BUFFER_SIZE];')
s=s.replace('/* CDC copies the payload into its FIFO; OK means queued successfully. */','/* CDC 将日志复制进 FIFO，OK 表示入队成功。 */')
s=s.replace('    tx_tick = osKernelSysTick();','''    g_log_tx_diag.last_line_bytes = (uint32_t)len;
    if (len > g_log_tx_diag.max_line_bytes) g_log_tx_diag.max_line_bytes = (uint32_t)len;
    tx_tick = osKernelSysTick();''',1)
save('User/Support/log.c',s)
s=read('Core/Src/freertos.c')
before='osThreadDef(DebugTask, debug_task, osPriorityLow, 0, 512);'
assert before in s
s=s.replace(before,'/* 压测日志增加格式化缓冲和快照数组，调试任务栈扩为 1024 个字。 */\n  osThreadDef(DebugTask, debug_task, osPriorityLow, 0, 1024);',1)
save('Core/Src/freertos.c',s)
(stage/'original_hashes.json').write_text(json.dumps(hashes,indent=2),encoding='utf-8')
schema={k:v for k,v in channels.items()}
schema['fdcan1']=['hfdcan1.Instance->'+x for x in can+canextra]
schema['fdcan23']=['hfdcan2.Instance->'+x for x in can]+['hfdcan3.Instance->'+x for x in can]
(stage/'schema.json').write_text(json.dumps({'channels':schema,'common_words_32_to_47':common},ensure_ascii=False,indent=2),encoding='utf-8')
# 按 uint32 最大十进制值计算最长行，验证格式化缓冲和 USB 槽容量。
lengths={tag:len('INFO: ['+tag+'] tick=4294967295 seq=4294967295 ch=9 n=48 w='+('/'.join(['FFFFFFFF']*48))+'\r\n') for tag in channels}
assert max(lengths.values())<768 and max(lengths.values())<=2048
(stage/'line_lengths.json').write_text(json.dumps(lengths,indent=2),encoding='utf-8')
print(json.dumps({'max_line_bytes':max(lengths.values()),'max_raw_bytes_per_second':max(lengths.values())*100,'channels':list(channels)},ensure_ascii=False))
