#include "comm_task.h"
#include "mode_switch_task.h"
#include "cmsis_os.h"
#include "prot_vision.h"
#include "prot_power.h"
#include "drv_dji_motor.h"
#include "drv_dm_motor.h"
#include "drv_lk_motor.h"
#include "prot_dr16.h"
#include "status_task.h"
#include "board_comm.h"

void comm_task(void const* argument)
{
    uint32_t thread_wake_time = osKernelSysTick();
    for(;;)
    {
        thread_wake_time = osKernelSysTick();
			
        taskENTER_CRITICAL();
        status.task.comm = 1;
        dji_motor_output_data();
       
        for (int i = 0; i < 3; i+=2){
            if( joint_motor[i].state ==0){
                dm_motor_set_control_cmd(&joint_motor[i], CMD_ENABLE_MODE);	  					
            }else {
				dm_motor_output_single_data(&joint_motor[i]);   
            }        
//			dm_motor_output_single_data(&joint_motor[i]);   
        }        
        
        taskEXIT_CRITICAL();		
        osDelayUntil(&thread_wake_time, 1);		
        
        taskENTER_CRITICAL();
        lk_set_current(&trigger_motor);
         for (int i = 1; i < 4; i+=2){
            if( joint_motor[i].state == 0){
                dm_motor_set_control_cmd(&joint_motor[i], CMD_ENABLE_MODE);		
            }else{
				dm_motor_output_single_data(&joint_motor[i]);   
            } 
//			dm_motor_output_single_data(&joint_motor[i]);   
        }
        
        taskEXIT_CRITICAL();
        osDelayUntil(&thread_wake_time, 1);
    }
}
