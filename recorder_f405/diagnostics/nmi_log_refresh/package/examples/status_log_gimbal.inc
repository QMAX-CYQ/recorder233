/* Optional rm_main_gimbal mapping: include in status_task.c, not separately.
 * Other boards must adapt status member names, protocol objects and motors. */
#include "status_log.h"
#include "status_task.h"
#include "prot_vision.h"
#include "prot_vtm.h"
#include "prot_imu.h"
#include "board_comm.h"
#include "drv_dji_motor.h"
#include "drv_dm_motor.h"

static void StatusLog_ObserveGimbal(uint32_t now, uint32_t now_hal)
{
    StatusLog_Observe(LOG_MODULE_VISION, 1U, status.vision, vision.last_rx_tick, 0U, 0U, now, now_hal);
    StatusLog_Observe(LOG_MODULE_VTM, 1U, status.vtm, vtm.last_rx_tick, 0U, 0U, now, now_hal);
    StatusLog_Observe(LOG_MODULE_GIMBAL_IMU, 1U, status.imu, gimbal_imu.last_rx_tick, 0U, 0U, now, now_hal);
    StatusLog_Observe(LOG_MODULE_BOARD, 1U, status.board, fdcan_board_comm.last_rx_tick, 0U, 0U, now, now_hal);
    /* 链表指针在注册时设置；未注册的电机不参与告警。CAN 编号保留工程枚举原值。 */
#define OBSERVE_MOTOR(module, motor) \
    StatusLog_Observe((module), ((motor).list.next != NULL && (motor).list.prev != NULL), \
        (motor).online, (motor).last_rx_tick, (motor).can_channel, (motor).can_id, now, now_hal)
    OBSERVE_MOTOR(LOG_MODULE_FRIC_RIGHT, fric_motor[0]);
    OBSERVE_MOTOR(LOG_MODULE_FRIC_LEFT, fric_motor[1]);
    OBSERVE_MOTOR(LOG_MODULE_DRIVER_0, driver_motor[0]);
    OBSERVE_MOTOR(LOG_MODULE_DRIVER_1, driver_motor[1]);
    OBSERVE_MOTOR(LOG_MODULE_YAW, yaw_motor);
    OBSERVE_MOTOR(LOG_MODULE_DJI_TRIGGER, trigger_motor);
    OBSERVE_MOTOR(LOG_MODULE_JOINT_0, joint_motor[0]);
    OBSERVE_MOTOR(LOG_MODULE_JOINT_1, joint_motor[1]);
    OBSERVE_MOTOR(LOG_MODULE_JOINT_2, joint_motor[2]);
    OBSERVE_MOTOR(LOG_MODULE_JOINT_3, joint_motor[3]);
    OBSERVE_MOTOR(LOG_MODULE_DM_TRIGGER, trigger_motor_2325);
    OBSERVE_MOTOR(LOG_MODULE_PIT, pit_motor);
#undef OBSERVE_MOTOR
}
