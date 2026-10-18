#include "log_task.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "fdcan.h"
#include "usart.h"
#include "log.h"
#include "fault_log.h"
#include "drv_dji_motor.h"
#include "drv_dm_motor.h"
#include "gimbal_task.h"
#include "prot_imu.h"
#include "usbd_cdc_if.h"
#include <string.h>

extern osThreadId CommTaskHandle;
extern osThreadId ModeSwitchTaskHandle;
extern osThreadId StatusTaskHandle;
extern osThreadId ChassisTaskHandle;
extern osThreadId GimbalTaskHandle;
extern osThreadId ShootTaskHandle;
extern osThreadId UITaskHandle;
extern osThreadId LogTaskHandle;
extern osThreadId BoardCommTaskHandle;


volatile log_task_diag_t g_log_task_diag;

/* 每次只读取观测数据，不改变外设或内核配置。
 * 不读取 UART RDR、FDCAN PSR/ECR、SysTick CTRL 等有读副作用的寄存器。 */
static uint32_t log_stack_words(osThreadId handle)
{
    return handle ? (uint32_t)uxTaskGetStackHighWaterMark((TaskHandle_t)handle) : 0U;
}

/* 浮点数据发送 IEEE-754 原始位模式，避免浮点格式化和数值转换溢出。 */
static uint32_t log_float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/* 读取状态和配置寄存器，不操作 FIFO 确认寄存器或发送请求寄存器。 */
static void log_fdcan_snapshot(FDCAN_GlobalTypeDef *can, uint32_t *words, uint32_t count)
{
    if (can == NULL) {
        memset(words, 0, count * sizeof(*words));
        return;
    }
    words[0] = can->CCCR;
    words[1] = can->NBTP;
    words[2] = can->DBTP;
    words[3] = can->IR;
    words[4] = can->IE;
    words[5] = can->ILS;
    words[6] = can->ILE;
    words[7] = can->GFC;
    words[8] = can->RXF0S;
    words[9] = can->RXF1S;
    words[10] = can->TXFQS;
    words[11] = can->TXBRP;
    words[12] = can->TXBTO;
    words[13] = can->TXBCF;
    words[14] = can->TXBTIE;
    words[15] = can->TXBCIE;
    if (count > 16U) {
        words[16] = can->CREL;
        words[17] = can->ENDN;
        words[18] = can->TEST;
        words[19] = can->RWD;
        words[20] = can->TSCC;
        words[21] = can->TSCV;
        words[22] = can->TOCC;
        words[23] = can->TOCV;
        words[24] = can->TDCR;
        words[25] = can->SIDFC;
        words[26] = can->XIDFC;
        words[27] = can->XIDAM;
        words[28] = can->HPMS;
        words[29] = can->NDAT1;
        words[30] = can->NDAT2;
        words[31] = can->TXEFS;
    }
}

/* 快照不是跨字段原子事务，业务任务和中断可以在采样时更新数据。 */
static void log_snapshot(uint8_t channel, uint32_t *w)
{
    switch (channel) {
    case 0U: /* sys：索引定义见压力测试说明。 */
        w[0] = (uint32_t)(RCC->CR);
        w[1] = (uint32_t)(RCC->CFGR);
        w[2] = (uint32_t)(RCC->D1CFGR);
        w[3] = (uint32_t)(RCC->D2CFGR);
        w[4] = (uint32_t)(RCC->D3CFGR);
        w[5] = (uint32_t)(RCC->PLLCKSELR);
        w[6] = (uint32_t)(RCC->PLLCFGR);
        w[7] = (uint32_t)(RCC->PLL1DIVR);
        w[8] = (uint32_t)(RCC->PLL1FRACR);
        w[9] = (uint32_t)(RCC->PLL2DIVR);
        w[10] = (uint32_t)(RCC->PLL2FRACR);
        w[11] = (uint32_t)(RCC->PLL3DIVR);
        w[12] = (uint32_t)(RCC->PLL3FRACR);
        w[13] = (uint32_t)(RCC->D1CCIPR);
        w[14] = (uint32_t)(RCC->D2CCIP1R);
        w[15] = (uint32_t)(RCC->D2CCIP2R);
        w[16] = (uint32_t)(RCC->D3CCIPR);
        w[17] = (uint32_t)(RCC->AHB3ENR);
        w[18] = (uint32_t)(RCC->AHB1ENR);
        w[19] = (uint32_t)(RCC->AHB2ENR);
        w[20] = (uint32_t)(RCC->AHB4ENR);
        w[21] = (uint32_t)(RCC->APB3ENR);
        w[22] = (uint32_t)(RCC->APB1LENR);
        w[23] = (uint32_t)(RCC->APB1HENR);
        w[24] = (uint32_t)(RCC->APB2ENR);
        w[25] = (uint32_t)(RCC->APB4ENR);
        w[26] = (uint32_t)(RCC->RSR);
        w[27] = (uint32_t)(RCC->AHB3RSTR);
        w[28] = (uint32_t)(RCC->AHB1RSTR);
        w[29] = (uint32_t)(RCC->AHB2RSTR);
        w[30] = (uint32_t)(RCC->AHB4RSTR);
        w[31] = (uint32_t)(RCC->APB3RSTR);
        break;
    case 1U: /* task：索引定义见压力测试说明。 */
        w[0] = (uint32_t)(log_stack_words(CommTaskHandle));
        w[1] = (uint32_t)(log_stack_words(ModeSwitchTaskHandle));
        w[2] = (uint32_t)(log_stack_words(StatusTaskHandle));
        w[3] = (uint32_t)(log_stack_words(ChassisTaskHandle));
        w[4] = (uint32_t)(log_stack_words(GimbalTaskHandle));
        w[5] = (uint32_t)(log_stack_words(ShootTaskHandle));
        w[6] = (uint32_t)(log_stack_words(UITaskHandle));
        w[7] = (uint32_t)(log_stack_words(LogTaskHandle));
        w[8] = (uint32_t)(log_stack_words(BoardCommTaskHandle));
        w[9] = (uint32_t)(uxTaskGetNumberOfTasks());
        w[10] = (uint32_t)(xPortGetFreeHeapSize());
        w[11] = (uint32_t)(xPortGetMinimumEverFreeHeapSize());
        w[12] = (uint32_t)(g_log_task_diag.loops);
        w[13] = (uint32_t)(g_log_task_diag.max_gap_ms);
        w[14] = (uint32_t)(g_log_task_diag.late_count);
        w[15] = (uint32_t)(g_log_task_diag.catchup_count);
        w[16] = (uint32_t)(g_log_task_diag.max_run_ms);
        w[17] = (uint32_t)(cdc_tx_pending_count);
        w[18] = (uint32_t)(cdc_tx_inflight);
        w[19] = (uint32_t)(cdc_tx_queue_overflow_count);
        w[20] = (uint32_t)(g_cdc_tx_diag.accepted);
        w[21] = (uint32_t)(g_cdc_tx_diag.completed);
        w[22] = (uint32_t)(g_cdc_tx_diag.started);
        w[23] = (uint32_t)(g_cdc_tx_diag.accepted_bytes);
        w[24] = (uint32_t)(g_cdc_tx_diag.completed_bytes);
        w[25] = (uint32_t)(g_cdc_tx_diag.buffered);
        w[26] = (uint32_t)(g_cdc_tx_diag.queue_high_water);
        w[27] = (uint32_t)(g_cdc_tx_diag.start_failed);
        w[28] = (uint32_t)(g_log_tx_diag.format_truncated);
        w[29] = (uint32_t)(g_log_tx_diag.format_error);
        w[30] = (uint32_t)(g_log_tx_diag.last_line_bytes);
        w[31] = (uint32_t)(g_log_tx_diag.max_line_bytes);
        break;
    case 2U: /* fdcan1：索引定义见压力测试说明。 */
    {
        uint32_t can32[32];
        log_fdcan_snapshot(hfdcan1.Instance, can32, 32U);
        w[0] = (uint32_t)(can32[0]);
        w[1] = (uint32_t)(can32[1]);
        w[2] = (uint32_t)(can32[2]);
        w[3] = (uint32_t)(can32[3]);
        w[4] = (uint32_t)(can32[4]);
        w[5] = (uint32_t)(can32[5]);
        w[6] = (uint32_t)(can32[6]);
        w[7] = (uint32_t)(can32[7]);
        w[8] = (uint32_t)(can32[8]);
        w[9] = (uint32_t)(can32[9]);
        w[10] = (uint32_t)(can32[10]);
        w[11] = (uint32_t)(can32[11]);
        w[12] = (uint32_t)(can32[12]);
        w[13] = (uint32_t)(can32[13]);
        w[14] = (uint32_t)(can32[14]);
        w[15] = (uint32_t)(can32[15]);
        w[16] = (uint32_t)(can32[16]);
        w[17] = (uint32_t)(can32[17]);
        w[18] = (uint32_t)(can32[18]);
        w[19] = (uint32_t)(can32[19]);
        w[20] = (uint32_t)(can32[20]);
        w[21] = (uint32_t)(can32[21]);
        w[22] = (uint32_t)(can32[22]);
        w[23] = (uint32_t)(can32[23]);
        w[24] = (uint32_t)(can32[24]);
        w[25] = (uint32_t)(can32[25]);
        w[26] = (uint32_t)(can32[26]);
        w[27] = (uint32_t)(can32[27]);
        w[28] = (uint32_t)(can32[28]);
        w[29] = (uint32_t)(can32[29]);
        w[30] = (uint32_t)(can32[30]);
        w[31] = (uint32_t)(can32[31]);
    }
        break;
    case 3U: /* fdcan23：索引定义见压力测试说明。 */
    {
        uint32_t can2[16], can3[16];
        log_fdcan_snapshot(hfdcan2.Instance, can2, 16U);
        log_fdcan_snapshot(hfdcan3.Instance, can3, 16U);
        w[0] = (uint32_t)(can2[0]);
        w[1] = (uint32_t)(can2[1]);
        w[2] = (uint32_t)(can2[2]);
        w[3] = (uint32_t)(can2[3]);
        w[4] = (uint32_t)(can2[4]);
        w[5] = (uint32_t)(can2[5]);
        w[6] = (uint32_t)(can2[6]);
        w[7] = (uint32_t)(can2[7]);
        w[8] = (uint32_t)(can2[8]);
        w[9] = (uint32_t)(can2[9]);
        w[10] = (uint32_t)(can2[10]);
        w[11] = (uint32_t)(can2[11]);
        w[12] = (uint32_t)(can2[12]);
        w[13] = (uint32_t)(can2[13]);
        w[14] = (uint32_t)(can2[14]);
        w[15] = (uint32_t)(can2[15]);
        w[16] = (uint32_t)(can3[0]);
        w[17] = (uint32_t)(can3[1]);
        w[18] = (uint32_t)(can3[2]);
        w[19] = (uint32_t)(can3[3]);
        w[20] = (uint32_t)(can3[4]);
        w[21] = (uint32_t)(can3[5]);
        w[22] = (uint32_t)(can3[6]);
        w[23] = (uint32_t)(can3[7]);
        w[24] = (uint32_t)(can3[8]);
        w[25] = (uint32_t)(can3[9]);
        w[26] = (uint32_t)(can3[10]);
        w[27] = (uint32_t)(can3[11]);
        w[28] = (uint32_t)(can3[12]);
        w[29] = (uint32_t)(can3[13]);
        w[30] = (uint32_t)(can3[14]);
        w[31] = (uint32_t)(can3[15]);
    }
        break;
    case 4U: /* uart123：索引定义见压力测试说明。 */
        w[0] = (uint32_t)(huart1.Instance->ISR);
        w[1] = (uint32_t)(huart1.Instance->CR1);
        w[2] = (uint32_t)(huart1.Instance->CR2);
        w[3] = (uint32_t)(huart1.Instance->CR3);
        w[4] = (uint32_t)(huart1.Instance->BRR);
        w[5] = (uint32_t)(huart1.Instance->PRESC);
        w[6] = (uint32_t)(huart1.Instance->RTOR);
        w[7] = (uint32_t)(huart1.Instance->GTPR);
        w[8] = (uint32_t)(huart2.Instance->ISR);
        w[9] = (uint32_t)(huart2.Instance->CR1);
        w[10] = (uint32_t)(huart2.Instance->CR2);
        w[11] = (uint32_t)(huart2.Instance->CR3);
        w[12] = (uint32_t)(huart2.Instance->BRR);
        w[13] = (uint32_t)(huart2.Instance->PRESC);
        w[14] = (uint32_t)(huart2.Instance->RTOR);
        w[15] = (uint32_t)(huart2.Instance->GTPR);
        w[16] = (uint32_t)(huart3.Instance->ISR);
        w[17] = (uint32_t)(huart3.Instance->CR1);
        w[18] = (uint32_t)(huart3.Instance->CR2);
        w[19] = (uint32_t)(huart3.Instance->CR3);
        w[20] = (uint32_t)(huart3.Instance->BRR);
        w[21] = (uint32_t)(huart3.Instance->PRESC);
        w[22] = (uint32_t)(huart3.Instance->RTOR);
        w[23] = (uint32_t)(huart3.Instance->GTPR);
        w[24] = (uint32_t)(huart1.gState);
        w[25] = (uint32_t)(huart1.RxState);
        w[26] = (uint32_t)(huart2.gState);
        w[27] = (uint32_t)(huart2.RxState);
        w[28] = (uint32_t)(huart3.gState);
        w[29] = (uint32_t)(huart3.RxState);
        w[30] = (uint32_t)(g_log_tx_diag.accepted);
        w[31] = (uint32_t)(g_log_tx_diag.busy);
        break;
    case 5U: /* uart6910：索引定义见压力测试说明。 */
        w[0] = (uint32_t)(huart6.Instance->ISR);
        w[1] = (uint32_t)(huart6.Instance->CR1);
        w[2] = (uint32_t)(huart6.Instance->CR2);
        w[3] = (uint32_t)(huart6.Instance->CR3);
        w[4] = (uint32_t)(huart6.Instance->BRR);
        w[5] = (uint32_t)(huart6.Instance->PRESC);
        w[6] = (uint32_t)(huart6.Instance->RTOR);
        w[7] = (uint32_t)(huart6.Instance->GTPR);
        w[8] = (uint32_t)(huart9.Instance->ISR);
        w[9] = (uint32_t)(huart9.Instance->CR1);
        w[10] = (uint32_t)(huart9.Instance->CR2);
        w[11] = (uint32_t)(huart9.Instance->CR3);
        w[12] = (uint32_t)(huart9.Instance->BRR);
        w[13] = (uint32_t)(huart9.Instance->PRESC);
        w[14] = (uint32_t)(huart9.Instance->RTOR);
        w[15] = (uint32_t)(huart9.Instance->GTPR);
        w[16] = (uint32_t)(huart10.Instance->ISR);
        w[17] = (uint32_t)(huart10.Instance->CR1);
        w[18] = (uint32_t)(huart10.Instance->CR2);
        w[19] = (uint32_t)(huart10.Instance->CR3);
        w[20] = (uint32_t)(huart10.Instance->BRR);
        w[21] = (uint32_t)(huart10.Instance->PRESC);
        w[22] = (uint32_t)(huart10.Instance->RTOR);
        w[23] = (uint32_t)(huart10.Instance->GTPR);
        w[24] = (uint32_t)(huart6.gState);
        w[25] = (uint32_t)(huart6.RxState);
        w[26] = (uint32_t)(huart9.gState);
        w[27] = (uint32_t)(huart9.RxState);
        w[28] = (uint32_t)(huart10.gState);
        w[29] = (uint32_t)(huart10.RxState);
        w[30] = (uint32_t)(g_log_tx_diag.accepted);
        w[31] = (uint32_t)(g_log_tx_diag.busy);
        break;
    case 6U: /* nvic：索引定义见压力测试说明。 */
        w[0] = (uint32_t)(NVIC->ISER[0]);
        w[1] = (uint32_t)(NVIC->ISPR[0]);
        w[2] = (uint32_t)(NVIC->IABR[0]);
        w[3] = (uint32_t)(NVIC->ISER[1]);
        w[4] = (uint32_t)(NVIC->ISPR[1]);
        w[5] = (uint32_t)(NVIC->IABR[1]);
        w[6] = (uint32_t)(NVIC->ISER[2]);
        w[7] = (uint32_t)(NVIC->ISPR[2]);
        w[8] = (uint32_t)(NVIC->IABR[2]);
        w[9] = (uint32_t)(NVIC->ISER[3]);
        w[10] = (uint32_t)(NVIC->ISPR[3]);
        w[11] = (uint32_t)(NVIC->IABR[3]);
        w[12] = (uint32_t)(NVIC->ISER[4]);
        w[13] = (uint32_t)(NVIC->ISPR[4]);
        w[14] = (uint32_t)(NVIC->IABR[4]);
        w[15] = (uint32_t)(NVIC_GetPriority(FDCAN1_IT0_IRQn));
        w[16] = (uint32_t)(NVIC_GetPriority(FDCAN1_IT1_IRQn));
        w[17] = (uint32_t)(NVIC_GetPriority(FDCAN2_IT0_IRQn));
        w[18] = (uint32_t)(NVIC_GetPriority(FDCAN3_IT0_IRQn));
        w[19] = (uint32_t)(NVIC_GetPriority(USART1_IRQn));
        w[20] = (uint32_t)(NVIC_GetPriority(USART2_IRQn));
        w[21] = (uint32_t)(NVIC_GetPriority(USART6_IRQn));
        w[22] = (uint32_t)(NVIC_GetPriority(OTG_HS_IRQn));
        w[23] = (uint32_t)(SCB->SHPR[0]);
        w[24] = (uint32_t)(SCB->SHPR[1]);
        w[25] = (uint32_t)(SCB->SHPR[2]);
        w[26] = (uint32_t)(SCB->SHPR[3]);
        w[27] = (uint32_t)(SCB->SHPR[4]);
        w[28] = (uint32_t)(SCB->SHPR[5]);
        w[29] = (uint32_t)(SCB->SHPR[6]);
        w[30] = (uint32_t)(SCB->SHPR[7]);
        w[31] = (uint32_t)(SCB->SHPR[8]);
        break;
    case 7U: /* dji：索引定义见压力测试说明。 */
        w[0] = (uint32_t)(driver_motor[0].online);
        w[1] = (uint32_t)(driver_motor[0].state);
        w[2] = (uint32_t)(driver_motor[0].ecd);
        w[3] = (uint32_t)(driver_motor[0].speed_rpm);
        w[4] = (uint32_t)(driver_motor[0].rx_current);
        w[5] = (uint32_t)(driver_motor[0].tx_current);
        w[6] = (uint32_t)(driver_motor[0].send_cnt);
        w[7] = (uint32_t)(driver_motor[0].receive_cnt);
        w[8] = (uint32_t)(fric_motor[0].online);
        w[9] = (uint32_t)(fric_motor[0].state);
        w[10] = (uint32_t)(fric_motor[0].ecd);
        w[11] = (uint32_t)(fric_motor[0].speed_rpm);
        w[12] = (uint32_t)(fric_motor[0].rx_current);
        w[13] = (uint32_t)(fric_motor[0].tx_current);
        w[14] = (uint32_t)(fric_motor[0].send_cnt);
        w[15] = (uint32_t)(fric_motor[0].receive_cnt);
        w[16] = (uint32_t)(fric_motor[1].online);
        w[17] = (uint32_t)(fric_motor[1].state);
        w[18] = (uint32_t)(fric_motor[1].ecd);
        w[19] = (uint32_t)(fric_motor[1].speed_rpm);
        w[20] = (uint32_t)(fric_motor[1].rx_current);
        w[21] = (uint32_t)(fric_motor[1].tx_current);
        w[22] = (uint32_t)(fric_motor[1].send_cnt);
        w[23] = (uint32_t)(fric_motor[1].receive_cnt);
        w[24] = (uint32_t)(yaw_motor.online);
        w[25] = (uint32_t)(yaw_motor.state);
        w[26] = (uint32_t)(yaw_motor.ecd);
        w[27] = (uint32_t)(yaw_motor.speed_rpm);
        w[28] = (uint32_t)(yaw_motor.rx_current);
        w[29] = (uint32_t)(yaw_motor.tx_current);
        w[30] = (uint32_t)(yaw_motor.send_cnt);
        w[31] = (uint32_t)(yaw_motor.receive_cnt);
        break;
    case 8U: /* dm：索引定义见压力测试说明。 */
        w[0] = (uint32_t)(pit_motor.online);
        w[1] = (uint32_t)(pit_motor.err_state);
        w[2] = (uint32_t)(pit_motor.send_cnt);
        w[3] = (uint32_t)(pit_motor.receive_cnt);
        w[4] = (uint32_t)(pit_motor.last_rx_tick);
        w[5] = (uint32_t)(log_float_bits(pit_motor.p));
        w[6] = (uint32_t)(log_float_bits(pit_motor.v));
        w[7] = (uint32_t)(log_float_bits(pit_motor.kp));
        w[8] = (uint32_t)(log_float_bits(pit_motor.kd));
        w[9] = (uint32_t)(log_float_bits(pit_motor.t));
        w[10] = (uint32_t)(log_float_bits(pit_motor.position));
        w[11] = (uint32_t)(log_float_bits(pit_motor.velocity));
        w[12] = (uint32_t)(log_float_bits(pit_motor.torque));
        w[13] = (uint32_t)(trigger_motor_2325.online);
        w[14] = (uint32_t)(trigger_motor_2325.err_state);
        w[15] = (uint32_t)(trigger_motor_2325.send_cnt);
        w[16] = (uint32_t)(trigger_motor_2325.receive_cnt);
        w[17] = (uint32_t)(trigger_motor_2325.last_rx_tick);
        w[18] = (uint32_t)(log_float_bits(trigger_motor_2325.p));
        w[19] = (uint32_t)(log_float_bits(trigger_motor_2325.v));
        w[20] = (uint32_t)(log_float_bits(trigger_motor_2325.kp));
        w[21] = (uint32_t)(log_float_bits(trigger_motor_2325.kd));
        w[22] = (uint32_t)(log_float_bits(trigger_motor_2325.t));
        w[23] = (uint32_t)(log_float_bits(trigger_motor_2325.position));
        w[24] = (uint32_t)(log_float_bits(trigger_motor_2325.velocity));
        w[25] = (uint32_t)(log_float_bits(trigger_motor_2325.torque));
        w[26] = (uint32_t)(log_float_bits(gimbal.yaw_angle.ref));
        w[27] = (uint32_t)(log_float_bits(gimbal.yaw_angle.fdb));
        w[28] = (uint32_t)(log_float_bits(gimbal.pit_angle.ref));
        w[29] = (uint32_t)(log_float_bits(gimbal.pit_angle.fdb));
        w[30] = (uint32_t)(log_float_bits(gimbal.yaw_output));
        w[31] = (uint32_t)(log_float_bits(gimbal.pit_output));
        break;
    case 9U: /* imu：索引定义见压力测试说明。 */
        w[0] = (uint32_t)(log_float_bits(gimbal_imu.pit));
        w[1] = (uint32_t)(log_float_bits(gimbal_imu.yaw));
        w[2] = (uint32_t)(log_float_bits(gimbal_imu.rol));
        w[3] = (uint32_t)(log_float_bits(gimbal_imu.wx));
        w[4] = (uint32_t)(log_float_bits(gimbal_imu.wy));
        w[5] = (uint32_t)(log_float_bits(gimbal_imu.wz));
        w[6] = (uint32_t)(log_float_bits(gimbal_imu.ax));
        w[7] = (uint32_t)(log_float_bits(gimbal_imu.ay));
        w[8] = (uint32_t)(log_float_bits(gimbal_imu.az));
        w[9] = (uint32_t)(log_float_bits(chassis_imu.pit));
        w[10] = (uint32_t)(log_float_bits(chassis_imu.yaw));
        w[11] = (uint32_t)(log_float_bits(chassis_imu.rol));
        w[12] = (uint32_t)(log_float_bits(chassis_imu.wx));
        w[13] = (uint32_t)(log_float_bits(chassis_imu.wy));
        w[14] = (uint32_t)(log_float_bits(chassis_imu.wz));
        w[15] = (uint32_t)(log_float_bits(chassis_imu.ax));
        w[16] = (uint32_t)(log_float_bits(chassis_imu.ay));
        w[17] = (uint32_t)(log_float_bits(chassis_imu.az));
        w[18] = (uint32_t)(gimbal_imu.online);
        w[19] = (uint32_t)(gimbal_imu.last_rx_tick);
        w[20] = (uint32_t)(chassis_imu.online);
        w[21] = (uint32_t)(chassis_imu.last_rx_tick);
        w[22] = (uint32_t)(log_float_bits(gimbal.yaw_angle.ref));
        w[23] = (uint32_t)(log_float_bits(gimbal.yaw_angle.fdb));
        w[24] = (uint32_t)(log_float_bits(gimbal.yaw_spd.ref));
        w[25] = (uint32_t)(log_float_bits(gimbal.yaw_spd.fdb));
        w[26] = (uint32_t)(log_float_bits(gimbal.pit_angle.ref));
        w[27] = (uint32_t)(log_float_bits(gimbal.pit_angle.fdb));
        w[28] = (uint32_t)(log_float_bits(gimbal.pit_spd.ref));
        w[29] = (uint32_t)(log_float_bits(gimbal.pit_spd.fdb));
        w[30] = (uint32_t)(log_float_bits(gimbal.yaw_output));
        w[31] = (uint32_t)(log_float_bits(gimbal.pit_output));
        break;
    default:
        memset(w, 0, 32U * sizeof(*w));
        break;
    }
    w[32] = SCB->ICSR;
    w[33] = SCB->SHCSR;
    w[34] = SCB->CFSR;
    w[35] = SCB->HFSR;
    w[36] = SCB->DFSR;
    w[37] = SCB->VTOR;
    w[38] = SCB->AIRCR;
    w[39] = SCB->CCR;
    w[40] = SCB->CPACR;
    w[41] = CoreDebug->DEMCR;
    w[42] = DWT->CTRL;
    w[43] = DWT->CYCCNT;
    w[44] = DWT->CPICNT;
    w[45] = DWT->EXCCNT;
    w[46] = DWT->LSUCNT;
    w[47] = DWT->FOLDCNT;
}

/* 保留 tick 在行首，兼容 F405 的跨 USB 包行解析。
 * 一条日志包含 48 个固定宽度数据字，无填充字符串。 */
static void log_send_snapshot(uint8_t channel, uint32_t tick, uint32_t sequence)
{
    static const char *const tags[LOG_CHANNEL_COUNT] = {
        "sys", "task", "fdcan1", "fdcan23", "uart123", "uart6910", "nvic", "dji", "dm", "imu"
    };
    uint32_t words[48] = {0};//此处初始化，防止用户用不到48个字时操作未定义地址
    log_snapshot(channel, words);
    DEBUG_INFO("[%s] tick=%lu seq=%lu ch=%u n=48 w=%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX\r\n",
        tags[channel], (unsigned long)tick, (unsigned long)sequence, (unsigned int)channel,
        (unsigned long)words[0], (unsigned long)words[1], (unsigned long)words[2], (unsigned long)words[3],
        (unsigned long)words[4], (unsigned long)words[5], (unsigned long)words[6], (unsigned long)words[7],
        (unsigned long)words[8], (unsigned long)words[9], (unsigned long)words[10], (unsigned long)words[11],
        (unsigned long)words[12], (unsigned long)words[13], (unsigned long)words[14], (unsigned long)words[15],
        (unsigned long)words[16], (unsigned long)words[17], (unsigned long)words[18], (unsigned long)words[19],
        (unsigned long)words[20], (unsigned long)words[21], (unsigned long)words[22], (unsigned long)words[23],
        (unsigned long)words[24], (unsigned long)words[25], (unsigned long)words[26], (unsigned long)words[27],
        (unsigned long)words[28], (unsigned long)words[29], (unsigned long)words[30], (unsigned long)words[31],
        (unsigned long)words[32], (unsigned long)words[33], (unsigned long)words[34], (unsigned long)words[35],
        (unsigned long)words[36], (unsigned long)words[37], (unsigned long)words[38], (unsigned long)words[39],
        (unsigned long)words[40], (unsigned long)words[41], (unsigned long)words[42], (unsigned long)words[43],
        (unsigned long)words[44], (unsigned long)words[45], (unsigned long)words[46], (unsigned long)words[47]);
}

void log_task(void const *argument)
{
    uint32_t wake_time = osKernelSysTick();
    uint8_t channel = 0U;
    uint8_t align_wake_time = 1U;
    (void)argument;

    for (;;) {
        uint32_t tick;
        /* 异常中只读取此心跳，不调用 RTOS 获取时间。 */
        FaultLog_Heartbeat(osKernelSysTick());
        LogLinkService_HS();
        CDC_TxService_HS();
        /* 测试请求必须在握手/补报等待分支前处理，USB 卡住时仍可手动复现。 */
        FaultLog_TestService(osKernelSysTick());
        if (!g_log_link_ready) {
            /* 等待期间不生成日志、不推进 seq/通道，也不补跑等待时间。 */
            g_log_link_diag.waiting_loops++;
            wake_time = osKernelSysTick();
            g_log_task_diag.last_tick = 0U;
            align_wake_time = 1U;
            osDelay(LOG_PERIOD_MS);
            continue;
        }
        if (FaultLog_Service()) {
            /* 故障补报优先于 INFO/WARN；结束后重新对齐周期，不补跑等待时间。 */
            wake_time = osKernelSysTick();
            g_log_task_diag.last_tick = 0U;
            align_wake_time = 1U;
            osDelay(LOG_PERIOD_MS);
            continue;
        }
        tick = osKernelSysTick();
        if (align_wake_time) {
            /* 握手等待结束后，从当前 tick 开始新的周期，避免首次发送追赶。 */
            wake_time = tick;
            align_wake_time = 0U;
        }
        uint32_t start_ms = HAL_GetTick();
        uint32_t run_ms;
        CDC_TxService_HS();
        if (g_log_task_diag.last_tick != 0U) {
            uint32_t gap = tick - g_log_task_diag.last_tick;
            if (gap > g_log_task_diag.max_gap_ms) g_log_task_diag.max_gap_ms = gap;
            if (gap > LOG_PERIOD_MS) g_log_task_diag.late_count++;
            if (gap < LOG_PERIOD_MS) g_log_task_diag.catchup_count++;
        }
        g_log_task_diag.last_tick = tick;
        g_log_task_diag.group_count[channel]++;
        /* 告警统一在此格式化发送，不占用轮转通道和 INFO 序号。 */
        LogEvent_Service();
        log_send_snapshot(channel, tick, g_log_task_diag.loops);
        g_log_task_diag.loops++;
        run_ms = HAL_GetTick() - start_ms;
        g_log_task_diag.last_run_ms = run_ms;
        if (run_ms > g_log_task_diag.max_run_ms) g_log_task_diag.max_run_ms = run_ms;
        if (run_ms >= LOG_PERIOD_MS) g_log_task_diag.overrun_count++;
        /* 每 10ms 一条，10 通道轮转；单通道周期为 100ms。 */
        channel = (uint8_t)((channel + 1U) % LOG_CHANNEL_COUNT);
        osDelayUntil(&wake_time, LOG_PERIOD_MS);
    }
}
