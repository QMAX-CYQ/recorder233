#include "debug_task.h"
#include "cmsis_os.h"
#include "task.h"
#include "data_log.h"
#include "stdint.h"
#include "prot_vision.h"
#include "wlr.h"
#include "leg_vmc.h"
#include "wheel_leg_model.h"
#include "drv_dji_motor.h"
#include "drv_dm_motor.h"
#include "prot_judge.h"
#include "prot_power.h"
#include "prot_imu.h"
#include "kalman_filter.h"
#include "func_generator.h"
#include "prot_dr16.h"
#include "prot_judge.h"
#include "us_time.h"
// #include "gimbal_task.h"
#include "chassis_task.h"
#include "mode_switch_task.h"
#include "board_comm.h"
#include "shoot_task.h"
#include "prot_ms53l0m.h"
#include "drv_lk_motor.h"

#define row_debug 2 * 10
#define DEBUG_TEXT_LOG_DIV       2u
#define DEBUG_TEXT_LOG_SLOT_NUM  21u
uint8_t debug_wave = 0;
uint8_t debug_text_log_enable = 1;
float test_hex = 1;
extern FGT_sin_t FGT_sin_chassis;
extern  uint16_t quadrant_cnt;
extern ctrl_mode_e  ctrl_mode;
extern float x3_balance_zero;
extern float real_vel;
extern float yw_ddot;
extern float Fwy;
extern float F_test[2];
extern float F_wy[2];
extern int32_t Last_cnt;
extern chassis_scale_t chassis_scale;
extern uint8_t global_back_flag;
extern osThreadId CommTaskHandle;
extern osThreadId ChassisTaskHandle;
extern osThreadId UITaskHandle;
extern osThreadId DebugTaskHandle;

/*
 * Text log quick map:
 * [fsm]          top/chassis/gimbal/shoot mode
 * [chassis-mode] ctrl/high/recover/rotate/jump/sky flags
 * [chassis-ref]  v/wz/yaw/s reference and feedback
 * [motor-wheel]  wheel motor feedback
 * [motor-joint-*] joint motor feedback
 * [lqr-diff-*]   lqr.X_diff groups
 * [lqr-wheel-*]  each wheel-row K[i] * X_diff[i] contribution
 * [lqr-u]        lqr.U_ref and key X_ref/X_fdb
 * [motor-cmd-*]  commanded wheel/joint torque
 * [leg-*]        leg angle, force, and wheel torque
 * [power]        supercap and power limit state
 * [stack]        FreeRTOS stack high water mark, in words
 * [fly-*]        update_fly_state conditions and per-leg states
 *
 * Short flag map:
 * f=fly_flag, a=fly_adapt, c=fly_cnt, fc=fly_flag_cnt
 * fn=Fn_kal/Fn_fdb, dist=Front_dis_kal/Front_dis_fdb
 * wy=predict_wy, ta=T_adapt, ro=roll_offs, hs=high_set
 * ll=L leg length ref/fdb, rl=R leg length ref/fdb
 */

static const char* debug_top_mode_name(TopMode_e mode)
{
    switch (mode) {
        case TOP_MODE_PROTECT:  return "PROTECT";
        case TOP_MODE_REMOTE:   return "REMOTE";
        case TOP_MODE_KEYBOARD: return "KEYBOARD";
        default:                return "UNKNOWN";
    }
}

static const char* debug_chassis_name(ChassisState_e state)
{
    switch (state) {
        case CHASSIS_STOP:                     return "STOP";
        case CHASSIS_LOW:                      return "LOW";
        case CHASSIS_FIGHT:                    return "FIGHT";
        case CHASSIS_LOW_SPIN:                 return "LOW_SPIN";
        case CHASSIS_HIGH:                     return "HIGH";
        case CHASSIS_TERRAIN_READY:            return "TERRAIN_RDY";
        case CHASSIS_TERRAIN_EXECUTING:        return "TERRAIN_RUN";
        case CHASSIS_ASCEND:                   return "ASCEND";
        case CHASSIS_EXECUTING_FOLLOW_ASCEND:  return "DOUBLE";
        case CHASSIS_ENERGY:                   return "ENERGY";
		case CHASSIS_STAIR:					   return "STAIR";
        default:                               return "UNKNOWN";
    }
}

static const char* debug_gimbal_name(GimbalState_e state)
{
    switch (state) {
        case GIMBAL_STOP:            return "STOP";
        case GIMBAL_GYRO_STABILIZE:  return "GYRO";
        case GIMBAL_MOUSE_CONTROL:   return "MOUSE";
        case GIMBAL_AUTO_AIM:        return "AUTO";
        default:                     return "UNKNOWN";
    }
}

static const char* debug_shoot_name(ShootState_e state)
{
    switch (state) {
        case SHOOT_PROTECT: return "PROTECT";
        case SHOOT_STOP:    return "STOP";
        case SHOOT_READY:   return "READY";
        case SHOOT_SINGLE:  return "SINGLE";
        case SHOOT_SERIES:  return "SERIES";
        default:            return "UNKNOWN";
    }
}

static const char* debug_jump_name(uint8_t state)
{
    switch (state) {
        case WLR_JUMP_IDLE:          return "IDLE";
        case WLR_JUMP_ASCEND:        return "ASCEND";
        case WLR_JUMP_RECOVER_SHORT: return "REC_S";
        case WLR_JUMP_RECOVER_LONG:  return "REC_L";
        default:                     return "UNKNOWN";
    }
}

static const char* debug_sky_name(uint16_t state)
{
    switch (state) {
        case WLR_SKY_IDLE:        return "IDLE";
        case WLR_SKY_FOLDING:     return "FOLD";
        case WLR_SKY_EXTENDING:   return "EXT";
        case WLR_SKY_AIR_FOLDING: return "AIR_FOLD";
        case WLR_SKY_LANDING:     return "LAND";
        case WLR_SKY_STAND:       return "STAND";
        default:                  return "UNKNOWN";
    }
}

static void debug_log_fsm_status(void)
{
    // log_printf("[fsm] top=%s chassis=%s gimbal=%s shoot=%s online=%u lock=%u ch1=%u\r\n",
    //            debug_top_mode_name(g_robot_ctx.output.top_mode),
    //            debug_chassis_name(g_robot_ctx.output.chassis),
    //            debug_gimbal_name(g_robot_ctx.output.gimbal),
    //            debug_shoot_name(g_robot_ctx.output.shoot),
    //            g_robot_ctx.is_online,
    //            lock_flag,
    //            rc.ch1);

    log_printf("wtf!\r\n");
}

static void debug_log_chassis_status(void)
{
    log_printf("[chassis-mode] ctrl=%u high=%u rec=%u rescue=%u rot=%u jump=%s sky=%s\r\n",
               wlr.ctrl_mode,
               wlr.high_flag,
               chassis.recover_flag,
               chassis.rescue_inter_flag,
               rotate_flag,
               debug_jump_name(wlr.jump_flag),
               debug_sky_name(wlr.sky_flag));
}

static void debug_log_chassis_ref_status(void)
{
    log_printf("[chassis-ref] v=%.2f/%.2f wz=%.2f/%.2f yaw=%.2f/%.2f s=%.2f/%.2f\r\n",
               wlr.v_ref,
               wlr.v_fdb,
               wlr.wz_ref,
               wlr.wz_fdb,
               wlr.yaw_ref,
               wlr.yaw_fdb,
               wlr.s_ref,
               wlr.s_fdb);
}

static void debug_log_wheel_motor_status(void)
{
    log_printf("[motor-wheel] L(on=%u vel=%.2f tq=%.2f) R(on=%u vel=%.2f tq=%.2f)\r\n",
               driver_motor[0].online,
               driver_motor[0].velocity,
               driver_motor[0].torque,
               driver_motor[1].online,
               driver_motor[1].velocity,
               driver_motor[1].torque);
}

static void debug_log_joint_motor_status(void)
{
    log_printf("[motor-joint-l] LB(on=%u s=%u p=%.2f v=%.2f tq=%.2f) LS(on=%u s=%u p=%.2f v=%.2f tq=%.2f)\r\n",
               joint_motor[0].online, joint_motor[0].state, joint_motor[0].position, joint_motor[0].velocity,
               joint_motor[0].torque,
               joint_motor[1].online, joint_motor[1].state, joint_motor[1].position, joint_motor[1].velocity,
               joint_motor[1].torque);
}

static void debug_log_joint_motor_right_status(void)
{
    log_printf("[motor-joint-r] RB(on=%u s=%u p=%.2f v=%.2f tq=%.2f) RS(on=%u s=%u p=%.2f v=%.2f tq=%.2f)\r\n",
               joint_motor[2].online, joint_motor[2].state, joint_motor[2].position, joint_motor[2].velocity,
               joint_motor[2].torque,
               joint_motor[3].online, joint_motor[3].state, joint_motor[3].position, joint_motor[3].velocity,
               joint_motor[3].torque);
}

static void debug_log_lqr_diff_status(void)
{
    log_printf("[lqr-diff-body] ds=%.3f dv=%.3f dyaw=%.3f dwz=%.3f dpit=%.3f ddpit=%.3f\r\n",
               lqr.X_diff[0],
               lqr.X_diff[1],
               lqr.X_diff[2],
               lqr.X_diff[3],
               lqr.X_diff[8],
               lqr.X_diff[9]);
}

static void debug_log_lqr_diff_leg_status(void)
{
    log_printf("[lqr-diff-leg] dql=%.3f ddql=%.3f dqr=%.3f ddqr=%.3f\r\n",
               lqr.X_diff[4],
               lqr.X_diff[5],
               lqr.X_diff[6],
               lqr.X_diff[7]);
}

static void debug_log_lqr_wheel_left_status(void)
{
    log_printf("[lqr-wheel-l] s=%.3f v=%.3f yaw=%.3f wz=%.3f ql=%.3f dql=%.3f qr=%.3f dqr=%.3f pit=%.3f dpit=%.3f sum=%.3f\r\n",
               lqr.K[0] * lqr.X_diff[0],
               lqr.K[1] * lqr.X_diff[1],
               lqr.K[2] * lqr.X_diff[2],
               lqr.K[3] * lqr.X_diff[3],
               lqr.K[4] * lqr.X_diff[4],
               lqr.K[5] * lqr.X_diff[5],
               lqr.K[6] * lqr.X_diff[6],
               lqr.K[7] * lqr.X_diff[7],
               lqr.K[8] * lqr.X_diff[8],
               lqr.K[9] * lqr.X_diff[9],
               lqr.U_ref[0]);
}

static void debug_log_lqr_wheel_right_status(void)
{
    log_printf("[lqr-wheel-r] s=%.3f v=%.3f yaw=%.3f wz=%.3f ql=%.3f dql=%.3f qr=%.3f dqr=%.3f pit=%.3f dpit=%.3f sum=%.3f\r\n",
               lqr.K[10] * lqr.X_diff[0],
               lqr.K[11] * lqr.X_diff[1],
               lqr.K[12] * lqr.X_diff[2],
               lqr.K[13] * lqr.X_diff[3],
               lqr.K[14] * lqr.X_diff[4],
               lqr.K[15] * lqr.X_diff[5],
               lqr.K[16] * lqr.X_diff[6],
               lqr.K[17] * lqr.X_diff[7],
               lqr.K[18] * lqr.X_diff[8],
               lqr.K[19] * lqr.X_diff[9],
               lqr.U_ref[1]);
}

static void debug_log_lqr_output_status(void)
{
    log_printf("[lqr-u] uw_l=%.3f uw_r=%.3f ul_l=%.3f ul_r=%.3f x8=%.3f x9=%.3f r1=%.3f f1=%.3f\r\n",
               lqr.U_ref[0],
               lqr.U_ref[1],
               lqr.U_ref[2],
               lqr.U_ref[3],
               lqr.X_ref[8],
               lqr.X_fdb[8],
               lqr.X_ref[1],
               lqr.X_fdb[1]);
}

static void debug_log_motor_cmd_status(void)
{
    log_printf("[motor-cmd-wheel] L=%.3f R=%.3f lqr=%.3f/%.3f side=%.3f/%.3f\r\n",
               driver_motor[0].t,
               driver_motor[1].t,
               lqr.U_ref[0],
               lqr.U_ref[1],
               wlr.side[0].Tw,
               wlr.side[1].Tw);
}

static void debug_log_joint_cmd_status(void)
{
    log_printf("[motor-cmd-joint] LB=%.3f LS=%.3f RB=%.3f RS=%.3f sideL=%.3f/%.3f sideR=%.3f/%.3f\r\n",
               joint_motor[0].t,
               joint_motor[1].t,
               joint_motor[2].t,
               joint_motor[3].t,
               wlr.side[0].T1,
               wlr.side[0].T2,
               wlr.side[1].T1,
               wlr.side[1].T2);
}

static void debug_log_leg_status(void)
{
    log_printf("[leg-l] q1=%.3f q2=%.3f w1=%.2f w2=%.2f Fy=%.2f T0=%.2f Tw=%.2f\r\n",
               wlr.side[0].q1,
               wlr.side[0].q2,
               wlr.side[0].w1,
               wlr.side[0].w2,
               wlr.side[0].Fy,
               wlr.side[0].T0,
               wlr.side[0].Tw);
}

static void debug_log_leg_right_status(void)
{
    log_printf("[leg-r] q1=%.3f q2=%.3f w1=%.2f w2=%.2f Fy=%.2f T0=%.2f Tw=%.2f\r\n",
               wlr.side[1].q1,
               wlr.side[1].q2,
               wlr.side[1].w1,
               wlr.side[1].w2,
               wlr.side[1].Fy,
               wlr.side[1].T0,
               wlr.side[1].Tw);
}

static void debug_log_power_status(void)
{
    log_printf("[power] cap=%u%% v=%.2f i=%.2f mode=%u p=%.1f buf=%.1f lim=%.1f scale=%.2f wheel=%.1f\r\n",
               supercap.volume_percent,
               supercap.volage,
               supercap.current,
               supercap.power_mode,
               power_control.judge_chassis_power,
               power_control.judge_power_buffer,
               power_control.judge_max_power,
               power_control.power_scale,
               power_control.total_power_wheel);
}

static void debug_log_stack_status(void)
{
    log_printf("[stack] dbg=%lu chassis=%lu comm=%lu ui=%lu words\r\n",
               (unsigned long)uxTaskGetStackHighWaterMark((TaskHandle_t)DebugTaskHandle),
               (unsigned long)uxTaskGetStackHighWaterMark((TaskHandle_t)ChassisTaskHandle),
               (unsigned long)uxTaskGetStackHighWaterMark((TaskHandle_t)CommTaskHandle),
               (unsigned long)uxTaskGetStackHighWaterMark((TaskHandle_t)UITaskHandle));
}

static void debug_log_fly_status(void)
{
    log_printf("[fly-cond] pit=%.3f roll=%.3f gnd=%.3f ro=%.2f hs=%.3f rot=%u high=%u rec=%u jump=%s sky=%s\r\n",
               chassis_imu.pit,
               wlr.roll_fdb,
               tlm.gnd_roll_fdb,
               wlr.roll_offs,
               wlr.high_set,
               rotate_flag,
               wlr.high_flag,
               chassis.recover_flag,
               debug_jump_name(wlr.jump_flag),
               debug_sky_name(wlr.sky_flag));
}

static void debug_log_fly_len_status(void)
{
    log_printf("[fly-len] Lref/fdb=%.3f/%.3f Rref/fdb=%.3f/%.3f ldiff=%.3f gnd=%.3f\r\n",
               tlm.l_ref[0],
               vmc[0].L_fdb,
               tlm.l_ref[1],
               vmc[1].L_fdb,
               vmc[0].L_fdb - vmc[1].L_fdb,
               tlm.gnd_roll_fdb);
}

static void debug_log_fly_left_status(void)
{
    log_printf("[fly-l] f=%u a=%u c=%d fc=%u fn=%.1f/%.1f dist=%.2f/%.2f wy=%.2f ta=%.2f\r\n",
               wlr.side[0].fly_flag,
               wlr.side[0].fly_adapt,
               wlr.side[0].fly_cnt,
               wlr.side[0].fly_flag_cnt,
               wlr.side[0].Fn_kal,
               wlr.side[0].Fn_fdb,
               wlr.side[0].Front_dis_kal,
               wlr.side[0].Front_dis_fdb,
               wlr.side[0].predict_wy,
               wlr.side[0].T_adapt);
}

static void debug_log_fly_right_status(void)
{
    log_printf("[fly-r] f=%u a=%u c=%d fc=%u fn=%.1f/%.1f dist=%.2f/%.2f wy=%.2f ta=%.2f\r\n",
               wlr.side[1].fly_flag,
               wlr.side[1].fly_adapt,
               wlr.side[1].fly_cnt,
               wlr.side[1].fly_flag_cnt,
               wlr.side[1].Fn_kal,
               wlr.side[1].Fn_fdb,
               wlr.side[1].Front_dis_kal,
               wlr.side[1].Front_dis_fdb,
               wlr.side[1].predict_wy,
               wlr.side[1].T_adapt);
}

static void debug_log_shoot_delay_update(void)
{
    static uint32_t last_shoot_delay_update_cnt = 0;

    if (last_shoot_delay_update_cnt == shoot_delay_update_cnt) {
        return;
    }

    last_shoot_delay_update_cnt = shoot_delay_update_cnt;
    log_printf("[shoot-delay] raw=%.1f filter=%.1f speed=%.2f update=%lu overflow=%lu timeout=%lu\r\n",
               shoot_delay_time,
               vision_send_time,
               shoot_data.initial_speed,
               (unsigned long)shoot_delay_update_cnt,
               (unsigned long)shoot_delay_overflow_cnt,
               (unsigned long)shoot_delay_timeout_cnt);
}

static void debug_log_text_output(void)
{
    static uint8_t log_slot = 0;

    if (!debug_text_log_enable || debug_wave != 0) {
        return;
    }

    switch (log_slot) {
        case 0:
            debug_log_fsm_status();
            break;
        case 1:
            debug_log_chassis_status();
            break;
        case 2:
            debug_log_chassis_ref_status();
            break;
        case 3:
            debug_log_wheel_motor_status();
            break;
        case 4:
            debug_log_joint_motor_status();
            break;
        case 5:
            debug_log_joint_motor_right_status();
            break;
        case 6:
            debug_log_lqr_diff_status();
            break;
        case 7:
            debug_log_lqr_diff_leg_status();
            break;
        case 8:
            debug_log_lqr_wheel_left_status();
            break;
        case 9:
            debug_log_lqr_wheel_right_status();
            break;
        case 10:
            debug_log_lqr_output_status();
            break;
        case 11:
            debug_log_motor_cmd_status();
            break;
        case 12:
            debug_log_joint_cmd_status();
            break;
        case 13:
            debug_log_leg_status();
            break;
        case 14:
            debug_log_leg_right_status();
            break;
        case 15:
            debug_log_power_status();
            break;
        case 16:
//            debug_log_stack_status();
            break;
        case 17:
            debug_log_fly_status();
            break;
        case 18:
            debug_log_fly_len_status();
            break;
        case 19:
            debug_log_fly_left_status();
            break;
        default:
            debug_log_fly_right_status();
            break;
    }

   // log_slot = (log_slot + 1) % DEBUG_TEXT_LOG_SLOT_NUM;
}

void log_scope_data_pkg(void)
{
	switch (debug_wave) 
	{
		case 1: { /* 拨盘 PID */
			
//			log_scope_get_data(shoot.trigger_ecd.fdb);
//            log_scope_get_data(shoot.trigger_ecd.ref);
//			log_scope_get_data(wlr.side[0].Front_dis_fdb);
//            log_scope_get_data(wlr.side[0].Front_dis_kal);
			log_scope_get_data(wlr.stair_flag);
			log_scope_get_data(wlr.pit_fdb);
			log_scope_get_data(wlr.v_fdb);
			log_scope_get_data(wlr.v_ref);
			log_scope_get_data(lqr.X_fdb[4]);
			log_scope_get_data(lqr.X_fdb[6]);
//			log_scope_get_data(wlr.side[1].Fn_kal);
			break;
		}
		case 4: { /* 摩擦轮 */
			log_scope_get_data(chassis_imu.yaw);
			log_scope_get_data(chassis_imu.pit);
			break;
		}
        case 5: { /* 腿长 */
            log_scope_get_data(vmc[0].L_fdb);
            log_scope_get_data(vmc[1].L_fdb);
            log_scope_get_data(wlr.v_fdb);
            log_scope_get_data(wlr.side[0].fly_flag);
            log_scope_get_data(wlr.side[1].fly_flag);
            break;
		}
        case 6: { /* 小陀螺 */
            log_scope_get_data(chassis_imu.pit);
            log_scope_get_data(wlr.roll_fdb);
            log_scope_get_data(tlm.gnd_roll_fdb);
            break;
		}        
	case 7: { /* 激光 */
            log_scope_get_data(wlr.side[1].Front_dis_kal);
			log_scope_get_data(wlr.side[0].Front_dis_kal);
			log_scope_get_data(wlr.sky_flag);
			log_scope_get_data(wlr.pit_fdb);
			log_scope_get_data(ms53l0m_data_valid[0]);
			log_scope_get_data(ms53l0m_data_valid[1]);
            break;
		}
	case 8: { /* 拨盘 */
            log_scope_get_data(shoot.trigger_ecd.ref);
            log_scope_get_data(shoot.trigger_ecd.fdb);
//		
//            log_scope_get_data(shoot.trigger_spd.ref);
//            log_scope_get_data(shoot.trigger_spd.fdb);
//            log_scope_get_data(shoot.trigger_output);
		
            log_scope_get_data(shoot_delay_time);
            log_scope_get_data(trigger_motor.speed_rpm);
            log_scope_get_data(shoot.trigger_output);
		
			
			
            log_scope_get_data(back_cnt);
            log_scope_get_data(back_flag);
            log_scope_get_data(err_cnt);
			 
		

            break;
		}
	case 9:
	{
//		log_scope_get_data(chassis_imu.rol);
//		log_scope_get_data(tlm.l_ref[0]);
//		log_scope_get_data(tlm.l_ref[1]);
//		log_scope_get_data(tlm.l_fdb[0]);
//		log_scope_get_data(tlm.l_fdb[1]);
//		log_scope_get_data(wlr.roll_offs);
		log_scope_get_data(shoot_data.initial_speed);
		log_scope_get_data(shoot.trigger_ecd.fdb);
		log_scope_get_data(shoot.trigger_ecd.ref);
		break;
	}
	}
}
/* 串口上位机数据发送任务 */
void debug_task(void const* argument)
{
    uint32_t thread_wake_time = osKernelSysTick();
    uint8_t text_log_div = 0;
    for(;;)
    {
        thread_wake_time = osKernelSysTick();
      //  log_scope_data_output();
      //  debug_log_shoot_delay_update();
        if (++text_log_div >= DEBUG_TEXT_LOG_DIV) {
            text_log_div = 0;
         //   debug_log_text_output();
        }
        osDelayUntil(&thread_wake_time, 2);
    }
}
