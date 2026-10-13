#include "debug_task.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "fdcan.h"
#include "usart.h"
#include "log.h"
#include "status_task.h"
#include "drv_dji_motor.h"
#include "drv_dm_motor.h"
#include "mode_switch_task.h"
#include "gimbal_task.h"
#include "usbd_cdc_if.h"

extern osThreadId CommTaskHandle;
extern osThreadId ModeSwitchTaskHandle;
extern osThreadId StatusTaskHandle;
extern osThreadId ChassisTaskHandle;
extern osThreadId GimbalTaskHandle;
extern osThreadId ShootTaskHandle;
extern osThreadId UITaskHandle;
extern osThreadId DebugTaskHandle;
extern osThreadId BoardCommTaskHandle;

static uint32_t debug_stack_words(osThreadId task_handle)
{
    if (task_handle == 0)
        return 0U;

    return (uint32_t)uxTaskGetStackHighWaterMark((TaskHandle_t)task_handle);
}

static void debug_log_system_status(uint32_t tick)
{
    DEBUG_INFO("[sys] tick=%lu mode=%u lock=%u online=%u r=%u v=%u b=%u vtm=%u imu=%u dji=%u dm=%u task=%u/%u/%u/%u/%u/%u/%u/%u\r\n",
               (unsigned long)tick,
               (unsigned int)ctrl_mode,
               (unsigned int)lock_flag,
               (unsigned int)status.all,
               (unsigned int)status.remote,
               (unsigned int)status.vision,
               (unsigned int)status.board,
               (unsigned int)status.vtm,
               (unsigned int)status.imu,
               (unsigned int)status.dji_motor,
               (unsigned int)status.dm_motor,
               (unsigned int)status.task.comm,
               (unsigned int)status.task.mode_switch,
               (unsigned int)status.task.status,
               (unsigned int)status.task.chassis,
               (unsigned int)status.task.gimbal,
               (unsigned int)status.task.shoot,
               (unsigned int)status.task.ui,
               (unsigned int)status.task.board);
}

static void debug_log_task_status(uint32_t tick)
{
    DEBUG_INFO("[task] tick=%lu stack_words dbg=%lu comm=%lu mode=%lu status=%lu chassis=%lu gimbal=%lu shoot=%lu ui=%lu board=%lu\r\n",
               (unsigned long)tick,
               (unsigned long)debug_stack_words(DebugTaskHandle),
               (unsigned long)debug_stack_words(CommTaskHandle),
               (unsigned long)debug_stack_words(ModeSwitchTaskHandle),
               (unsigned long)debug_stack_words(StatusTaskHandle),
               (unsigned long)debug_stack_words(ChassisTaskHandle),
               (unsigned long)debug_stack_words(GimbalTaskHandle),
               (unsigned long)debug_stack_words(ShootTaskHandle),
               (unsigned long)debug_stack_words(UITaskHandle),
               (unsigned long)debug_stack_words(BoardCommTaskHandle));
}

static void debug_log_fdcan_status(uint32_t tick)
{
    DEBUG_INFO("[fdcan] t=%lu c1 PSR/ECR/IR=%08lX/%08lX/%08lX c2=%08lX/%08lX/%08lX c3=%08lX/%08lX/%08lX\r\n",
               (unsigned long)tick,
               (unsigned long)hfdcan1.Instance->PSR,
               (unsigned long)hfdcan1.Instance->ECR,
               (unsigned long)hfdcan1.Instance->IR,
               (unsigned long)hfdcan2.Instance->PSR,
               (unsigned long)hfdcan2.Instance->ECR,
               (unsigned long)hfdcan2.Instance->IR,
               (unsigned long)hfdcan3.Instance->PSR,
               (unsigned long)hfdcan3.Instance->ECR,
               (unsigned long)hfdcan3.Instance->IR);
}

static uint32_t debug_uart_isr(UART_HandleTypeDef *huart)
{
    return (huart->Instance != NULL) ? huart->Instance->ISR : 0U;
}

static uint32_t debug_uart_cr3(UART_HandleTypeDef *huart)
{
    return (huart->Instance != NULL) ? huart->Instance->CR3 : 0U;
}

static void debug_log_uart_123_status(uint32_t tick)
{
    DEBUG_INFO("[uart123] t=%lu u1 ISR/CR3=%08lX/%08lX u2=%08lX/%08lX u3=%08lX/%08lX\r\n",
               (unsigned long)tick,
               (unsigned long)debug_uart_isr(&huart1),
               (unsigned long)debug_uart_cr3(&huart1),
               (unsigned long)debug_uart_isr(&huart2),
               (unsigned long)debug_uart_cr3(&huart2),
               (unsigned long)debug_uart_isr(&huart3),
               (unsigned long)debug_uart_cr3(&huart3));
}

static void debug_log_uart_610_status(uint32_t tick)
{
    DEBUG_INFO("[uart610] t=%lu u6 ISR/CR3=%08lX/%08lX u10=%08lX/%08lX\r\n",
               (unsigned long)tick,
               (unsigned long)debug_uart_isr(&huart6),
               (unsigned long)debug_uart_cr3(&huart6),
               (unsigned long)debug_uart_isr(&huart10),
               (unsigned long)debug_uart_cr3(&huart10));
}

static void debug_log_nvic_status(uint32_t tick)
{
    DEBUG_INFO("[nvic] t=%lu ICSR=%08lX b0=%08lX/%08lX/%08lX b1=%08lX/%08lX/%08lX b2=%08lX/%08lX/%08lX b4=%08lX/%08lX/%08lX prio c1/u2/usb=%lu/%lu/%lu\r\n",
               (unsigned long)tick,
               (unsigned long)SCB->ICSR,
               (unsigned long)NVIC->ISER[0],
               (unsigned long)NVIC->ISPR[0],
               (unsigned long)NVIC->IABR[0],
               (unsigned long)NVIC->ISER[1],
               (unsigned long)NVIC->ISPR[1],
               (unsigned long)NVIC->IABR[1],
               (unsigned long)NVIC->ISER[2],
               (unsigned long)NVIC->ISPR[2],
               (unsigned long)NVIC->IABR[2],
               (unsigned long)NVIC->ISER[4],
               (unsigned long)NVIC->ISPR[4],
               (unsigned long)NVIC->IABR[4],
               (unsigned long)NVIC_GetPriority(FDCAN1_IT0_IRQn),
               (unsigned long)NVIC_GetPriority(USART2_IRQn),
               (unsigned long)NVIC_GetPriority(OTG_HS_IRQn));
}

static void debug_log_motor_status(uint32_t tick)
{
    DEBUG_INFO("[motor] tick=%lu djiL=%u/%d/%u djiR=%u/%d/%u pit=%u/%u p100=%ld v100=%ld t100=%ld trig=%u/%u joint0=%u/%u yaw100=%ld/%ld\r\n",
               (unsigned long)tick,
               (unsigned int)driver_motor[0].online,
               (int)driver_motor[0].speed_rpm,
               (unsigned int)driver_motor[0].temperature,
               (unsigned int)driver_motor[1].online,
               (int)driver_motor[1].speed_rpm,
               (unsigned int)driver_motor[1].temperature,
               (unsigned int)pit_motor.online,
               (unsigned int)pit_motor.err_state,
               (long)(pit_motor.position * 100.0f),
               (long)(pit_motor.velocity * 100.0f),
               (long)(pit_motor.torque * 100.0f),
               (unsigned int)trigger_motor_2325.online,
               (unsigned int)trigger_motor_2325.err_state,
               (unsigned int)joint_motor[0].online,
               (unsigned int)joint_motor[0].err_state,
               (long)(gimbal.yaw_angle.ref * 100.0f),
               (long)(gimbal.yaw_angle.fdb * 100.0f));
}

//static void debug_log_usb_tx_status(uint32_t tick)
//{
//    DEBUG_INFO("[usbtx] tick=%lu queued=%lu started=%lu complete=%lu pending=%lu inflight=%lu overflow=%lu start_error=%lu\r\n",
//               (unsigned long)tick,
//               (unsigned long)cdc_tx_queued_count,
//               (unsigned long)cdc_tx_started_count,
//               (unsigned long)cdc_tx_complete_count,
//               (unsigned long)cdc_tx_pending_count,
//               (unsigned long)cdc_tx_inflight,
//               (unsigned long)cdc_tx_queue_overflow_count,
//               (unsigned long)cdc_tx_start_error_count);
//}

void debug_task(void const *argument)
{
    uint32_t wake_time = osKernelSysTick();
    uint8_t log_group = 0U;

    (void)argument;

    for (;;)
    {
        uint32_t tick = osKernelSysTick();

        switch (log_group)
        {
            case 0U:
                debug_log_system_status(tick);
                break;
            case 1U:
               debug_log_task_status(tick);
                break;
            case 2U:
                debug_log_fdcan_status(tick);
                break;
            case 3U:
                debug_log_uart_123_status(tick);
                break;
            case 4U:
                debug_log_uart_610_status(tick);
                break;
            case 5U:
                debug_log_nvic_status(tick);
                break;
//            case 6U:
//                debug_log_usb_tx_status(tick);
//                break;
            default:
                debug_log_motor_status(tick);
                break;
        }

        log_group = (uint8_t)((log_group + 1U) % 8U);
        osDelayUntil(&wake_time, 10U);
    }
}
