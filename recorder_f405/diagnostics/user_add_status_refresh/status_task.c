#include "status_task.h"
#include "control_def.h"
#include "cmsis_os.h"
#include "mode_switch_task.h"
#include "drv_dji_motor.h"
#include "drv_dm_motor.h"
#include "prot_dr16.h"
#include "prot_imu.h"
#include "prot_judge.h"
#include "prot_power.h"
#include "prot_vision.h"
#include "iwdg.h"
#include "board_comm.h"
#include "prot_vtm.h"
#include "log.h"

status_t status;

#include "examples/status_log_gimbal.inc"

int iwdg_test = 1;
void status_task(void const* argument)
{
    uint32_t startup_tick = osKernelSysTick();
    StatusLog_Init(startup_tick);
    (void)argument;
	MX_IWDG1_Init();
    for(;;)
    {
		
        if (status.task.comm == 1 &&
            status.task.gimbal == 1 &&
			status.task.board == 1 &&
            status.task.shoot == 1 &&
            status.task.mode_switch == 1) {
            status.task.comm = 0;
            status.task.gimbal = 0;
            status.task.chassis = 0;
            status.task.shoot = 0;
            status.task.mode_switch = 0;
			status.task.board = 0;
            HAL_IWDG_Refresh(&hiwdg1);
        }
				
        status.vision = vision_check_offline();
        status.vtm = vtm_check_offline();	
        status.imu = imu_check_offline();
        status.dji_motor = dji_motor_check_offline();
        status.dm_motor = dm_motor_check_offline();
        status.board = board_comm_check_offline();

        if (status.vision == 1 && status.vtm == 1 && status.imu == 1 
            && status.dji_motor == 0 && status.dm_motor == 1 && status.board == 1) {
            status.all = 1;
        } else {
            status.all = 0;
        }
        /* 保留原有离线判定和看门狗逻辑，告警只读取本轮判定结果。 */
        uint32_t now = osKernelSysTick();
        if ((uint32_t)(now - startup_tick) >= STATUS_LOG_STARTUP_GRACE_MS)
            StatusLog_ObserveGimbal(now, HAL_GetTick());
        osDelay(33);
    }
}
