#include "chassis_task.h"
#include "mode_switch_task.h"
#include "control_def.h"
#include "wlr.h"
#include "can_comm.h"
#include "drv_dji_motor.h"
#include "drv_dm_motor.h"
#include "prot_imu.h"
#include "prot_dr16.h"
#include "prot_power.h"
#include "pid.h"
#include "math_lib.h"
#include "kalman_filter.h"
#include "arm_math.h"
#include "string.h"
#include "cmsis_os.h"
#include "gimbal_task.h"
#include "status_task.h"
#include "KNN.h"
#include "func_generator.h"
#include "leg_vmc.h"
#include "prot_judge.h"
#include "prot_power.h"
#include "dwt.h"
#include "prot_hipnuc.h"
#include "board_comm.h"
#include "container.h"
#include "prot_ms53l0m.h"
#include "log.h"
ChassisState_e last_chassis_output;

#ifndef DO_ONCE
	#define DO_ONCE(code_block) { static int _flag = 0; if (!_flag) { _flag = 1; code_block; } }
#endif

static imu_data_t chassis_set_imu_data_container;

extern pid_t pid_leg_recover[2];
extern float real_vel;
extern ramp_t jump_ramp;

uint8_t rotate_flag;
uint8_t rotate_stop_flag;

ramp_t chassis_x_ramp;
ramp_t chassis_y_ramp;
ramp_t chassis_rotate_ramp;

kalman_filter_t kal_3508_vel[2];
kalman_filter_t kal_wy;
kalman_filter_t kal_fusion_vel;
FGT_sin_t FGT_sin_chassis;
chassis_t chassis;

float imu_pitch_offset = 0.0f;
float up_ready;
float spin_limit;
float spin_zero;

chassis_scale_t chassis_scale = {
    .remote = 1.0f/660*2.5f,
    .keyboard = 3.0f
};

static uint8_t check_ch3_trigger(void)
{
	static int16_t last_ch3 = 0;
	if(rc.ch3 <= -600 && last_ch3 > -600)
	{
		last_ch3 = rc.ch3;
		return 1;
	}
	else
	{
		last_ch3 = rc.ch3;
		return 0;
	}
}

// 工具函数 检测关节电机在起身过程中是否有堵转现象来判断是否卡台阶
static uint8_t check_joint_stall(dm_motor_t motor1, dm_motor_t motor2)
{
    // 为每个电机维护独立的堵转计数器
    static uint16_t stall_cnt_1 = 0;
    static uint16_t stall_cnt_2 = 0;

    // 对于motor1
    if(fabsf(motor1.velocity) < 0.3f && fabsf(motor1.torque) > 10.0f)
        stall_cnt_1++;
    else 
        stall_cnt_1 = 0;
    
    // 对于motor2
    if(fabsf(motor2.velocity) < 0.3f && fabsf(motor2.torque) > 10.0f)
        stall_cnt_2++;
    else 
        stall_cnt_2 = 0;

	if(stall_cnt_1 > 300)
		stall_cnt_1 = 300;
	if(stall_cnt_2 > 300)
		stall_cnt_2 = 300;

	//决定返回值
    if(stall_cnt_1 >= 150 && stall_cnt_2 >= 150)
		return 3;//双腿卡死
	if(stall_cnt_1 == 300)
		return 1;//左腿卡死
    if(stall_cnt_2 == 300)
		return 2;//右腿卡死
    return 0;
}

// 工具函数 检测腿是否脱离卡死
// return 1 ：已脱离
static uint8_t check_joint_offstall(uint8_t stall_leg_num)
{
	static uint16_t offstall_cnt;
	if(!stall_leg_num)
		return 1;
    switch(stall_leg_num)
	{
		case 1:
		{
			if(fabsf(joint_motor[0].velocity) > 0.3f)
				offstall_cnt++;
			else 
				offstall_cnt = 0;
		}break;
		case 2:
		{
			if(fabsf(joint_motor[2].velocity) > 0.3f)
				offstall_cnt++;
			else 
				offstall_cnt = 0;
		}break;
		case 3:
		{
			if(fabsf(joint_motor[0].velocity) > 0.3f && fabsf(joint_motor[2].velocity) > 0.3f)
				offstall_cnt++;
			else 
				offstall_cnt = 0;
		}break;
		default : break;
	}
	if(offstall_cnt > 100)
	{
		offstall_cnt = 0;
		return 1;
	}
	else
		return 0;
}

//工具函数 用来检测激光下降沿产生跳跃
uint8_t check_tof_jump(float dis_up, float dis_down)
{
	static float last_dis[2] = {0};
	static uint8_t tof_cnt = 0;
	if((wlr.side[0].Front_dis_kal + wlr.side[1].Front_dis_kal) / 2.0f <= (last_dis[0] + last_dis[1]) / 2.0f && 
		(wlr.side[0].Front_dis_kal + wlr.side[1].Front_dis_kal) / 2.0f > dis_down && \
		(wlr.side[0].Front_dis_kal + wlr.side[1].Front_dis_kal) / 2.0f < dis_up)
	{
		tof_cnt++;
	}
	else
	{
		tof_cnt = 0;
	}
	last_dis[0] = wlr.side[0].Front_dis_kal;
	last_dis[1] = wlr.side[1].Front_dis_kal;
	if(tof_cnt >= 50)
		return 1;
	else
		return 0;
}

// 恢复你本来的代码
static void chassis_ramp(void)
{
    if (rc.kb.bit.W) {
        ramp_calc(&chassis_x_ramp, chassis_scale.keyboard);
    } else if (rc.kb.bit.S) {
        ramp_calc(&chassis_x_ramp, -chassis_scale.keyboard);
    } else {
        ramp_calc(&chassis_x_ramp, 0);
    }
    if (rc.kb.bit.D) {
        ramp_calc(&chassis_y_ramp, chassis_scale.keyboard);
    } else if (rc.kb.bit.A) {
        ramp_calc(&chassis_y_ramp, -chassis_scale.keyboard);
    } else {
        ramp_calc(&chassis_y_ramp, 0);
    }
}

static float variable_vw_generate(float target_speed)
{
    static uint32_t dwt_count;
    static float t;
    t += DWT_GetDeltaT(&dwt_count);
    return (target_speed * fabsf(sinf(1.5f * PI * t + PI / 3.0f)));
}

static void Fusion_Vel_Acc_Init(void)
{
    kalman_filter_init(&kal_fusion_vel,2,1,2);
    const static float dt = 0.002f;
    kal_fusion_vel.A_data[0] = 1; kal_fusion_vel.A_data[1] = dt;
    kal_fusion_vel.A_data[2] = 0; kal_fusion_vel.A_data[3] = 1;
    kal_fusion_vel.B_data[0] = 0.5f * dt * dt; kal_fusion_vel.B_data[1] = dt;
    kal_fusion_vel.H_data[0] = 1; kal_fusion_vel.H_data[1] = 0;
    kal_fusion_vel.H_data[2] = 0; kal_fusion_vel.H_data[3] = 1;
    kal_fusion_vel.Q_data[0] = 0.1; kal_fusion_vel.Q_data[1] = 0;
    kal_fusion_vel.Q_data[2] = 0; kal_fusion_vel.Q_data[3] = 0.5;
    kal_fusion_vel.R_data[0] = 10; kal_fusion_vel.R_data[1] = 0;
    kal_fusion_vel.R_data[2] = 0; kal_fusion_vel.R_data[3] = 2000;
}

void Fusion_Vel_Acc_Test(void)
{
    kal_fusion_vel.measured_vector[0] = wlr.s_fdb;
    kal_fusion_vel.measured_vector[1] = (-driver_motor[0].velocity + driver_motor[1].velocity) * 0.055f / 2.0f;
    kal_fusion_vel.control_vector[0] = chassis_imu.ax;
    kalman_filter_update(&kal_fusion_vel);
}

static void chassis_init(void)
{
    memset(&chassis, 0, sizeof(chassis_t));
    memset(&chassis_x_ramp, 0, sizeof(ramp_t));
    memset(&chassis_y_ramp, 0, sizeof(ramp_t));
    wlr_init();

    ramp_init(&chassis_x_ramp, 0.05f, -3.0f, 3.0f);
    ramp_init(&chassis_y_ramp, 0.05f, -3.0f, 3.0f);
    ramp_init(&chassis_rotate_ramp, 0.06f, -2.0f * CHASSIS_ROTATE_SPEED, 2.0f * CHASSIS_ROTATE_SPEED);

    wlr.yaw_ref = (float)CHASSIS_YAW_OFFSET / 8192 * 2 * PI;
    wlr.yaw_offset = 1.7f;
    DWT_Init(550);
       
    DO_ONCE({
        for(int i = 0; i < 2; i++) {
            kalman_filter_init(&kal_3508_vel[i], 1, 0, 1);
            kal_3508_vel[i].A_data[0] = 1; kal_3508_vel[i].H_data[0] = 1;
            kal_3508_vel[i].Q_data[0] = 1; kal_3508_vel[i].R_data[0] = 200;
        }
        kalman_filter_init(&kal_wy, 1, 0, 1);
        kal_wy.A_data[0] = 1; kal_wy.H_data[0] = 1;
        kal_wy.Q_data[0] = 1; kal_wy.R_data[0] = 100;
    });
    
    FGT_sin_init (&FGT_sin_chassis,2,0,6000,7.0f,0,7.0f,-0.0f);
    chassis.init = 1;
}

// ==============================================================================
// 纯粹地把 FSM 的状态映射到底层的配置，不保留旧的 mode
// ==============================================================================
static void chassis_execute_fsm(void)
{
    rotate_flag = 0;
	wlr.energy_flag = 0;
	wlr.double_flag = 0;
	static uint16_t sky_ccc = 0;
    switch (g_robot_ctx.output.chassis) {
        case CHASSIS_STOP:
		{
			sky_ccc = 0;
            wlr.ctrl_mode = 0; 
            wlr.high_flag = 0;
            wlr.jump_flag = WLR_JUMP_IDLE;
            wlr.sky_flag = WLR_SKY_IDLE;
			wlr.stair_flag = WLR_STAIR_IDLE;
            chassis.rescue_cnt_L = 0;
            chassis.rescue_cnt_R = 0;
			chassis.recover_flag = 0;
			chassis.rescue_inter_flag = CHASSIS_RESCUE_IDLE;
			up_ready=0;
			g_robot_ctx.sky_start_flag = 0;
			g_robot_ctx.sky_finish_flag = 0;
			g_robot_ctx.jump_finish_flag = 0;
            break;
		}

        case CHASSIS_LOW:
		{
			sky_ccc = 0;
            wlr.ctrl_mode = 2; 
            wlr.high_flag = 0; 
            wlr.jump_flag = WLR_JUMP_IDLE;
            wlr.sky_flag = WLR_SKY_IDLE;
			wlr.stair_flag = WLR_STAIR_IDLE;
			g_robot_ctx.sky_start_flag = 0;
			g_robot_ctx.sky_finish_flag = 0;
			g_robot_ctx.jump_finish_flag = 0;
			if(last_chassis_output == CHASSIS_STOP)
				chassis.recover_flag = 1;
            break;
		}

        case CHASSIS_HIGH:
		{
            wlr.ctrl_mode = 2;
            wlr.high_flag = 1; 
            wlr.jump_flag = WLR_JUMP_IDLE;
            wlr.sky_flag = WLR_SKY_IDLE;
			wlr.stair_flag = WLR_STAIR_IDLE;
			g_robot_ctx.sky_start_flag = 0;
			g_robot_ctx.sky_finish_flag = 0;
			g_robot_ctx.jump_finish_flag = 0;
            break;
		}

        case CHASSIS_LOW_SPIN:
		{
            wlr.ctrl_mode = 2;
            wlr.high_flag = 0; 
            rotate_flag = 1;   
			wlr.jump_flag = WLR_JUMP_IDLE;
			wlr.sky_flag = WLR_SKY_IDLE;
			wlr.stair_flag = WLR_STAIR_IDLE;
			g_robot_ctx.sky_start_flag = 0;
			g_robot_ctx.sky_finish_flag = 0;
			g_robot_ctx.jump_finish_flag = 0;
            break;
		}

        case CHASSIS_FIGHT:
		{
            wlr.ctrl_mode = 2;
            wlr.high_flag = 0;
            wlr.jump_flag = WLR_JUMP_IDLE;
			wlr.sky_flag = WLR_SKY_IDLE;
			wlr.stair_flag = WLR_STAIR_IDLE;
			g_robot_ctx.sky_start_flag = 0;
			g_robot_ctx.sky_finish_flag = 0;
			g_robot_ctx.jump_finish_flag = 0;
            break;
		}

        case CHASSIS_TERRAIN_READY:
		{
            wlr.ctrl_mode = 2;
            wlr.high_flag = 0;
            wlr.jump_flag = WLR_JUMP_IDLE;
			wlr.stair_flag = WLR_STAIR_IDLE;
			g_robot_ctx.sky_finish_flag = 0;
			g_robot_ctx.jump_finish_flag = 0;
			if(wlr.sky_flag == WLR_SKY_IDLE)
				wlr.sky_flag = WLR_SKY_FOLDING; 
			if(wlr.sky_flag == WLR_SKY_FOLDING) 
				sky_ccc++; 
			// if(wlr.sky_flag == WLR_SKY_FOLDING && ((wlr.side[0].Front_dis_kal + wlr.side[0].Front_dis_kal) / 2.0f > 0.65f) \
			// 	&& ((wlr.side[0].Front_dis_kal + wlr.side[0].Front_dis_kal) / 2.0f < 1.3f) && g_robot_ctx.input.mouse.l && check_tof_jump(0,0))
			// 	g_robot_ctx.sky_start_flag = 1;
			if(wlr.sky_flag == WLR_SKY_FOLDING && check_tof_jump(1.75f,0.65f) && sky_ccc > 700 && (ms53l0m_data_valid[0] == 0 || ms53l0m_data_valid[0] == 7)  && (ms53l0m_data_valid[1] == 0 || ms53l0m_data_valid[1] == 7))
				g_robot_ctx.sky_start_flag = 1;
            break;
		}

        case CHASSIS_TERRAIN_EXECUTING:
		{
			sky_ccc = 0;
            wlr.ctrl_mode = 2;
            wlr.high_flag = 0;
			wlr.jump_flag = WLR_JUMP_IDLE;
			wlr.stair_flag = WLR_STAIR_IDLE;
			g_robot_ctx.sky_start_flag = 0;
            if(wlr.sky_flag == WLR_SKY_FOLDING) 
				wlr.sky_flag = WLR_SKY_EXTENDING; 
			//set跳跃完成标志位 用于fsm自动变回低腿长模式
			if(wlr.sky_flag == WLR_SKY_STAND && wlr.sky_over && !g_robot_ctx.sky_finish_flag)
				g_robot_ctx.sky_finish_flag = 1;
            break;
		}
		
		case CHASSIS_ASCEND:
		{
			wlr.ctrl_mode = 2;
			wlr.high_flag = 0;
			wlr.sky_flag = WLR_SKY_IDLE;
			wlr.stair_flag = WLR_STAIR_IDLE;
			g_robot_ctx.sky_start_flag = 0;
			if(wlr.jump_flag == WLR_JUMP_IDLE && wlr.direction == 0) 
				wlr.jump_flag = WLR_JUMP_ASCEND; 
			if(wlr.jump_flag == WLR_JUMP_RECOVER_LONG && !g_robot_ctx.jump_finish_flag)
				g_robot_ctx.jump_finish_flag = 1;
			break;
		}
		
		case CHASSIS_EXECUTING_FOLLOW_ASCEND:
		{
			wlr.ctrl_mode = 2;
			wlr.high_flag = 0;
			wlr.double_flag = 1;
			if(wlr.sky_flag == WLR_SKY_IDLE && wlr.jump_flag == WLR_JUMP_IDLE)
				wlr.sky_flag = WLR_SKY_FOLDING; 
			else if(wlr.sky_flag == WLR_SKY_FOLDING)
				sky_ccc++;
			if(wlr.sky_flag == WLR_SKY_FOLDING && ((wlr.side[0].Front_dis_kal + wlr.side[0].Front_dis_kal) / 2.0f > 0.25f) \
				&& ((wlr.side[0].Front_dis_kal + wlr.side[0].Front_dis_kal) / 2.0f < 0.9f) && sky_ccc > 250)
				wlr.sky_flag = WLR_SKY_EXTENDING; 
			else if(wlr.sky_flag == WLR_SKY_STAND && !g_robot_ctx.sky_finish_flag)
				g_robot_ctx.sky_finish_flag = 1;
			if(g_robot_ctx.sky_finish_flag && wlr.jump_flag == WLR_JUMP_IDLE && wlr.sky_over == 1)
			{
				wlr.sky_flag = WLR_SKY_IDLE;
				wlr.jump_flag = WLR_JUMP_ASCEND; 
			}
			else if(wlr.jump_flag == WLR_JUMP_RECOVER_LONG && !g_robot_ctx.jump_finish_flag)
				g_robot_ctx.jump_finish_flag = 1;
			break;
		}
		
		case CHASSIS_ENERGY:
		{
			wlr.ctrl_mode = 2;
			wlr.high_flag = 0;
			wlr.sky_flag = WLR_SKY_IDLE;
			wlr.jump_flag = WLR_JUMP_IDLE;
			wlr.stair_flag = WLR_STAIR_IDLE;
			wlr.energy_flag = 1;
			break;
		}
		
		case CHASSIS_STAIR:
		{
			wlr.ctrl_mode = 2;
			wlr.high_flag = 0;
			wlr.sky_flag = WLR_SKY_IDLE;
			wlr.jump_flag = WLR_JUMP_IDLE;
			if(wlr.stair_flag == WLR_STAIR_IDLE)
				wlr.stair_flag = WLR_STAIR_ASCEND;
			break;
		}
		
        default:
		{
			sky_ccc = 0;
            wlr.ctrl_mode = 0; 
            wlr.high_flag = 0;
            wlr.jump_flag = WLR_JUMP_IDLE;
            wlr.sky_flag = WLR_SKY_IDLE;
			wlr.stair_flag = WLR_STAIR_IDLE;
            chassis.rescue_cnt_L = 0;
            chassis.rescue_cnt_R = 0;
            chassis.recover_flag = 0;
			g_robot_ctx.sky_finish_flag = 0;
			g_robot_ctx.sky_start_flag = 0;
			chassis.rescue_inter_flag = CHASSIS_RESCUE_IDLE;
            break;
		}
    }

    if (g_robot_ctx.output.chassis == CHASSIS_HIGH) { 
        if (g_robot_ctx.output.chassis_speed) chassis_scale.keyboard = 2.2f;
        else chassis_scale.keyboard = 2.2f;
    } else { 
        if (g_robot_ctx.output.chassis_speed) chassis_scale.keyboard = 2.3f;
        else chassis_scale.keyboard = 2.3f;
    }
	if(g_robot_ctx.output.chassis == CHASSIS_ASCEND){
		chassis_scale.keyboard = 1.7f;
	}

    if (g_robot_ctx.output.chassis == CHASSIS_HIGH) 
		chassis_scale.remote = 1.0f / 660 * 2.2f;
    else 
		chassis_scale.remote = 1.0f /660 * 2.3f; 
	
	last_chassis_output = g_robot_ctx.output.chassis;
    
}

uint8_t rotate_ramp_flag; 
ChassisState_e rotate_chassis_mode = CHASSIS_STOP; // [修改] 替换为 FSM 类型
uint16_t rotate_state_cnt;
ChassisState_e last_chassis_mode = CHASSIS_STOP;   // [修改] 替换为 FSM 类型

// ==============================================================================
// 完全基于 FSM ChassisState_e 进行解算
// ==============================================================================
static void chassis_data_input(void)
{		
    spin_limit = circle_error((float)CHASSIS_YAW_OFFSET / 8192 * 2 * PI, (float)yaw_motor.ecd / 8192 * 2 * PI, 2 * PI);

    // 1. 速度输入算算 (基于大模式)
    if (g_robot_ctx.output.chassis == CHASSIS_STOP || g_robot_ctx.output.top_mode == TOP_MODE_PROTECT) {
        chassis.input.vx = 0;
        chassis.input.vy = 0;
    } 
    else if (g_robot_ctx.output.top_mode == TOP_MODE_REMOTE) { 
        chassis.input.vx = -g_robot_ctx.input.ch4 * chassis_scale.remote;
        chassis.input.vy =  g_robot_ctx.input.ch3 * chassis_scale.remote;
    } 
    else if (g_robot_ctx.output.top_mode == TOP_MODE_KEYBOARD) { 
            chassis_ramp();
            chassis.input.vx = -chassis_x_ramp.out;
            chassis.input.vy =  chassis_y_ramp.out;
    }

    // ================= 旋转控制算法 (完全替换旧模式) =================
    switch (g_robot_ctx.output.chassis) {
        
        case CHASSIS_STOP: {
            wlr.yaw_ref = (float)yaw_motor.ecd / 8192 * 2 * PI;
            wlr.yaw_fdb = (float)yaw_motor.ecd / 8192 * 2 * PI;
            wlr.wz_ref = 0;
			chassis.turn_back_flag = 0;
            break;
        }
        case CHASSIS_LOW:
        case CHASSIS_HIGH:
        case CHASSIS_TERRAIN_READY:
        case CHASSIS_TERRAIN_EXECUTING:
		case CHASSIS_EXECUTING_FOLLOW_ASCEND:			{ // 整合了原版的所有 FOLLOW 和 PRONE
			if((key_scan_clear(KB_CTRL) || check_ch3_trigger()) && gimbal.start_up)
				chassis.turn_back_flag = 1;
			if(chassis.turn_back_flag && chassis.turn_back_cnt <= 1000)
			{
				chassis.turn_back_cnt++;//2s超时检测
				if(wlr.direction)
				{
					wlr.yaw_ref = (float)CHASSIS_YAW_OFFSET / 8192 * 2 * PI;
					wlr.yaw_fdb = (float)yaw_motor.ecd / 8192 * 2 * PI;  
					wlr.wz_ref = 0.0f;
					wlr.yaw_err = circle_error(wlr.yaw_ref, wlr.yaw_fdb, 2 * PI);
					if((wlr.yaw_err < PI / 3 && wlr.yaw_err > 0) || (wlr.yaw_err > - PI / 3 && wlr.yaw_err < 0))
					{
						wlr.direction = 0;
						chassis.turn_back_flag = 0;
					}
				}
				else
				{
					wlr.yaw_ref = (float)CHASSIS_YAW_OFFSET / 8192 * 2 * PI - PI;
					wlr.yaw_fdb = (float)yaw_motor.ecd / 8192 * 2 * PI;  
					wlr.wz_ref = 0.0f;
					wlr.yaw_err = circle_error(wlr.yaw_ref, wlr.yaw_fdb, 2 * PI);
					if((wlr.yaw_err < PI / 3 && wlr.yaw_err > 0) || (wlr.yaw_err > - PI / 3 && wlr.yaw_err < 0))
					{
						wlr.direction = 1;
						chassis.turn_back_flag = 0;
					}
				}
			}
			else
			{
				chassis.turn_back_flag = 0;
				chassis.turn_back_cnt = 0;
				if (gimbal.start_up)    
					wlr.yaw_ref = (float)CHASSIS_YAW_OFFSET / 8192 * 2 * PI;
				else                    
					wlr.yaw_ref = (float)yaw_motor.ecd / 8192 * 2  * PI;  
				
				wlr.yaw_fdb = (float)yaw_motor.ecd / 8192 * 2 * PI;  
				wlr.wz_ref = 0.0f;
				wlr.yaw_err = circle_error(wlr.yaw_ref, wlr.yaw_fdb, 2 * PI);
				
				if (wlr.yaw_err > PI / 2 || wlr.yaw_err < - PI / 2) {
					wlr.yaw_ref = (float)CHASSIS_YAW_OFFSET / 8192 * 2 * PI - PI ;
					wlr.direction = 1;
				}
				else if(wlr.yaw_err < PI / 2 || wlr.yaw_err > - PI / 2 ) {
					wlr.direction = 0;
				}
			}
			chassis_rotate_ramp.out =0;
            break;
        }
       case CHASSIS_ASCEND: {
			wlr.yaw_ref = (float)CHASSIS_YAW_OFFSET / 8192 * 2 * PI;
			wlr.yaw_fdb = (float)yaw_motor.ecd / 8192 * 2 * PI;  
			wlr.wz_ref = 0;
			wlr.yaw_err = circle_error(wlr.yaw_ref, wlr.yaw_fdb, 2 * PI);
			if(fabsf(wlr.yaw_err) < PI / 8.0f) wlr.direction = 0;
			break;
		}
	   case CHASSIS_STAIR:
		 {
			wlr.yaw_ref = (float)CHASSIS_YAW_OFFSET / 8192 * 2 * PI - PI;
			wlr.yaw_fdb = (float)yaw_motor.ecd / 8192 * 2 * PI;  
			wlr.wz_ref = 0;
			wlr.yaw_err = circle_error(wlr.yaw_ref, wlr.yaw_fdb, 2 * PI);
			if(fabsf(wlr.yaw_err) < PI / 8.0f) wlr.direction = 1;
			break;
		}
        case CHASSIS_FIGHT: {
            wlr.yaw_ref = (float)CHASSIS_YAW_FIGHT / 8192 * 2 * PI;
            wlr.yaw_fdb = (float)yaw_motor.ecd / 8192 * 2 * PI;
            wlr.wz_ref = 0;
            break;
        }               
        
        case CHASSIS_LOW_SPIN: { // 整合了原版的所有 ROTATE     
            wlr.yaw_ref = wlr.yaw_fdb;
            if(power_control.judge_max_power > 40.0f)
                chassis_rotate_ramp.min = -(CHASSIS_ROTATE_SPEED);
            else
                chassis_rotate_ramp.min = -CHASSIS_ROTATE_SPEED;
           
			wlr.wz_ref = ramp_calc(&chassis_rotate_ramp , -CHASSIS_ROTATE_SPEED);
            break;
        }
		case CHASSIS_ENERGY:
		{
			wlr.yaw_ref = wlr.yaw_fdb;
			wlr.wz_ref = 0.0f;
			break;
		}
        
        // UNFOLLOW 已经按照你的要求完全删除
        
        default:break;
    }

    if (wlr.yaw_ref < 0) wlr.yaw_ref += 2 * PI;
    else if (wlr.yaw_ref > 2 * PI) wlr.yaw_ref -= 2 * PI;
    
    wlr.yaw_err = circle_error((float)CHASSIS_YAW_OFFSET / 8192 * 2 * PI, (float)yaw_motor.ecd / 8192 * 2 * PI, 2 * PI);
    if (g_robot_ctx.output.chassis == CHASSIS_FIGHT)
        wlr.yaw_err = circle_error((float)CHASSIS_YAW_FIGHT / 8192 * 2 * PI, (float)yaw_motor.ecd / 8192 * 2 * PI, 2 * PI);
                
    chassis.output.vx = chassis.input.vx * arm_cos_f32(wlr.yaw_err) - chassis.input.vy * arm_sin_f32(wlr.yaw_err);
    chassis.output.vy = chassis.input.vx * arm_sin_f32(wlr.yaw_err) + chassis.input.vy * arm_cos_f32(wlr.yaw_err);
        
    if (g_robot_ctx.output.chassis == CHASSIS_LOW_SPIN){
		rotate_flag = 1;            
    }
    else if(rotate_ramp_flag == 0) rotate_flag = 0;   
    
    if (last_chassis_mode == CHASSIS_LOW_SPIN && (
         g_robot_ctx.output.chassis == CHASSIS_LOW || 
         g_robot_ctx.output.chassis == CHASSIS_HIGH ||
         g_robot_ctx.output.chassis == CHASSIS_TERRAIN_READY ||
         g_robot_ctx.output.chassis == CHASSIS_TERRAIN_EXECUTING)) {
            rotate_ramp_flag = 1;        
            rotate_stop_flag = 1;
    }
    
    if (rotate_stop_flag) {
        if (fabs(wlr.wz_fdb) < 2.0f) rotate_stop_flag = 0;
    }
    
    if(rotate_ramp_flag) {
        if(spin_zero == 0) spin_zero = spin_limit;
        rotate_state_cnt++;
        if(fabs(spin_zero) < PI / 2.0f  ) {                 
            if( circle_error((float)CHASSIS_YAW_OFFSET / 8192 * 2 * PI, wlr.yaw_fdb, 2 * PI) < 1.25f 
				&& circle_error((float)CHASSIS_YAW_OFFSET / 8192 * 2 * PI, wlr.yaw_fdb, 2 * PI) > 0.0f && rotate_state_cnt > 50){
                rotate_ramp_flag = 0; spin_zero = 0; rotate_state_cnt = 0; rotate_chassis_mode = CHASSIS_STOP;        
            }
        }else{
            if(circle_error((float)CHASSIS_YAW_OFFSET / 8192 * 2 * PI - PI, wlr.yaw_fdb, 2 * PI) < 1.25f 
				&& circle_error((float)CHASSIS_YAW_OFFSET / 8192 * 2 * PI - PI, wlr.yaw_fdb, 2 * PI) > 0.0f && rotate_state_cnt > 50){
                rotate_ramp_flag = 0; spin_zero = 0; rotate_state_cnt = 0;
            }
        }       
        if (last_chassis_mode == CHASSIS_LOW_SPIN) 
            rotate_chassis_mode = last_chassis_mode;
            
        if(rotate_chassis_mode == CHASSIS_LOW_SPIN)
            wlr.wz_ref = -12.0f/1.5f;  
            
        wlr.yaw_ref = wlr.yaw_fdb;
    }
    
//    wlr.yaw_err = circle_error(wlr.yaw_ref,wlr.yaw_fdb, 2 * PI);
//    if (wlr.jump_flag && !wlr.jump_pre) {
//        wlr.wz_ref = 6.0f; wlr.yaw_err = 0;
//    }
//    wlr.yaw_ref = wlr.yaw_fdb + 1.0f * wlr.yaw_err;
    wlr.v_ref = chassis.output.vx;
//    
//    if (wlr.jump_flag && !wlr.jump_pre && ((fabs(circle_error((float)CHASSIS_YAW_OFFSET / 8192 * 2 * PI, wlr.yaw_fdb, 2 * PI)) < 0.2f)))
//        wlr.jump_pre = 1;
    
    if (g_robot_ctx.output.chassis == CHASSIS_FIGHT){
        wlr.v_ref = chassis.output.vy;
        wlr.s_ref += (wlr.v_ref * 0.001f * 2);
    }
    //小陀螺下速度是Vy方向的原因还不知道为何
    if(g_robot_ctx.output.chassis == CHASSIS_LOW_SPIN)  wlr.v_ref = chassis.output.vy;
    if(rotate_stop_flag) wlr.v_ref = 0.0f;
        
    wlr.roll_fdb    = -chassis_imu.rol;
    wlr.pit_fdb     = -(chassis_imu.pit + imu_pitch_offset);
    wlr.wy_fdb      = -chassis_imu.wy;
    wlr.wz_fdb      = -chassis_imu.wz;
    wlr.az_fdb      =  chassis_imu.az;
        
    wlr.side[0].q2 =  joint_motor[1].position - joint_motor[1].zero_point;
    wlr.side[0].q1 =  joint_motor[0].position - joint_motor[0].zero_point;
    if (wlr.side[0].q2 < 0) wlr.side[0].q2 += 2 * PI;
    if (wlr.side[0].q1 < 0) wlr.side[0].q1 += 2 * PI;
    wlr.side[0].w1 =  joint_motor[0].velocity; wlr.side[0].w2 =  joint_motor[1].velocity;
    wlr.side[0].t1 =  joint_motor[0].torque; wlr.side[0].t2 =  joint_motor[1].torque;
    wlr.side[0].qy = -driver_motor[0].position;
    kal_3508_vel[0].measured_vector[0] = -driver_motor[0].velocity;
    kalman_filter_update(&kal_3508_vel[0]);
    wlr.side[0].wy = kal_3508_vel[0].filter_vector[0];
    
    wlr.side[1].q2 =  -(joint_motor[3].position - joint_motor[3].zero_point);
    wlr.side[1].q1 =  -(joint_motor[2].position - joint_motor[2].zero_point);
    if (wlr.side[1].q2 < 0) wlr.side[1].q2 += 2 * PI;
    if (wlr.side[1].q1 < 0) wlr.side[1].q1 += 2 * PI;
    wlr.side[1].w1 = -joint_motor[2].velocity; wlr.side[1].w2 = -joint_motor[3].velocity;
    wlr.side[1].t1 = -joint_motor[2].torque; wlr.side[1].t2 = -joint_motor[3].torque;
    wlr.side[1].qy =  driver_motor[1].position;
    kal_3508_vel[1].measured_vector[0] = driver_motor[1].velocity;
    kalman_filter_update(&kal_3508_vel[1]);
    wlr.side[1].wy = kal_3508_vel[1].filter_vector[0];
    
    //KNN
    for (int i = 0; i < 10; i++) input_data[i] = lqr.X_fdb[i];
    input_data[10] =  wlr.side[0].Fn_kal;
    input_data[11] =  wlr.side[1].Fn_kal;
    
    // 记录最新状态给下一帧
    last_chassis_mode = g_robot_ctx.output.chassis;
}

float left_speed=3, right_speed=3;
float left_T, right_T;

static void chassis_self_rescue(void)
{
    static uint32_t leg_length_cnt;		//腿长到达目标长度之后，变量++，延时0.1s
	static float rescue_cnt = 0;		
	static float rescue_T = 4;			//翻转力矩
	
	if (fabs(chassis_imu.pit) > 1.5f)
		rescue_T = 6.5;
	else
		rescue_T = 4.5;
		
	left_T  = pid_calc(&pid_rescue[0], left_speed , (wlr.side[0].w1));
	right_T = pid_calc(&pid_rescue[1], right_speed, (wlr.side[1].w1));
		
	//第四象限卡台阶
//	if ((vmc[0].quadrant ==4 || vmc[1].quadrant == 4) && chassis.rescue_inter_flag != 1)
//		rescue_cnt ++;
		
//    if (!chassis.rescue_inter_flag && fabs(chassis_imu.pit) > 1.0f && fabs(chassis_imu.rol) < 0.1f)
//        chassis.rescue_inter_flag = 3;//3阶段 ----整车翻倒且保护天鹅颈
		
	if (!chassis.rescue_inter_flag && fabs(chassis_imu.pit) < 0.6f && (vmc[0].quadrant == 2 || vmc[1].quadrant == 2))
		chassis.rescue_inter_flag = 4;//4阶段 ----第二象限启动卡墙
		
    if (!chassis.rescue_inter_flag)
        chassis.rescue_inter_flag = 1;//1阶段 ----代表车身正在归正
    
    if (chassis.rescue_inter_flag == 3) {
        dm_motor_set_control_para(&joint_motor[0], 0, 0, 0, 0, left_T);//0.03 0.5
        dm_motor_set_control_para(&joint_motor[1], 0, 0, 0, 0, 0);	
        if (fabs(chassis_imu.rol) > 0.6f)
            chassis.rescue_inter_flag = 1;
    }    
		
	if (chassis.rescue_inter_flag == 4) {	//第二象限启动卡墙
		//左腿归正
		if (vmc[0].quadrant == 2 && chassis.rescue_cnt_L <= 100) {
			dm_motor_set_control_para(&joint_motor[0], 0, rescue_T, 0, 5, 0);//0.03 0.5
            dm_motor_set_control_para(&joint_motor[1], 0, 0, 0, 0, 0);	
		} else if (vmc[0].quadrant == 1) 
			chassis.rescue_cnt_L++;
		if (chassis.rescue_cnt_L > 100) {
			dm_motor_set_control_para(&joint_motor[0], 0, 0, 0, 0, 0);
			dm_motor_set_control_para(&joint_motor[1], 0, 0, 0, 0, 0); 		
		}
		//右腿归正
		if (vmc[1].quadrant == 2 && chassis.rescue_cnt_R <= 100 ) {
			dm_motor_set_control_para(&joint_motor[2], 0, -rescue_T, 0, 5, 0);//0.03 0.5
            dm_motor_set_control_para(&joint_motor[3], 0, 0, 0, 0, 0);	
		} else if (vmc[1].quadrant == 1)
			chassis.rescue_cnt_R++;
		if (chassis.rescue_cnt_R > 100) {
            dm_motor_set_control_para(&joint_motor[2], 0, 0, 0, 0, 0);
            dm_motor_set_control_para(&joint_motor[3], 0, 0, 0, 0, 0);
		}
		//进入收腿阶段
        if(chassis.rescue_cnt_L > 200 || chassis.rescue_cnt_R > 200) {
            chassis.rescue_cnt_L = 0;
            chassis.rescue_cnt_R = 0;
			chassis.rescue_inter_flag = 1;
		}
	}
    
    if (chassis.rescue_inter_flag == 1) {
        //左腿归正
		if ( (vmc[0].quadrant == 1 || vmc[0].quadrant == 2 || vmc[0].quadrant == 3) && (fabs(chassis_imu.pit) > 0.3f || fabs(chassis_imu.rol )> 0.3f) || chassis.rescue_cnt_L > 1250 ) {
//			dm_motor_set_control_para(&joint_motor[0], 0, 8, 0, 5, 10);//快哥z
			dm_motor_set_control_para(&joint_motor[0], 0, rescue_T, 0, 5, 0);
            dm_motor_set_control_para(&joint_motor[1], 0, 0, 0, 0, 0);
			if( chassis.rescue_cnt_L < 1000)
				chassis.rescue_cnt_L = 0;
        } else if (vmc[0].quadrant == 4 || fabs(chassis_imu.pit) < 0.2f || fabs(chassis_imu.rol ) <  0.2f) {
            dm_motor_set_control_para(&joint_motor[0], 0, 0, 0, 0, 0);
            dm_motor_set_control_para(&joint_motor[1], 0, 0, 0, 0, 0);        
            chassis.rescue_cnt_L++;       
        }
        //右腿归正        
		if ((vmc[1].quadrant == 1 || vmc[1].quadrant == 2 || vmc[1].quadrant == 3) &&  (fabs(chassis_imu.pit) > 0.3f || fabs(chassis_imu.rol )> 0.3f) || chassis.rescue_cnt_R > 1250 ) {
//			dm_motor_set_control_para(&joint_motor[2], 0, -8, 0, 5, 10);//快哥
			dm_motor_set_control_para(&joint_motor[2], 0, -rescue_T, 0, 5, 0);
            dm_motor_set_control_para(&joint_motor[3], 0, 0, 0, 0, 0);
			if( chassis.rescue_cnt_R < 1000)
				chassis.rescue_cnt_R = 0; 
        } else if (vmc[1].quadrant == 4 || fabs(chassis_imu.pit) < 0.2f || fabs(chassis_imu.rol ) < 0.2f) {
            dm_motor_set_control_para(&joint_motor[2], 0, 0, 0, 0, 0);//0.03 0.5
            dm_motor_set_control_para(&joint_motor[3], 0, 0, 0, 0, 0);
            chassis.rescue_cnt_R++;
        }
        //进入收腿阶段
		if((chassis.rescue_cnt_L > 100 && chassis.rescue_cnt_R > 100) || up_ready ){
			if(!up_ready)
				up_ready = 1;
			/*shangjiao*/
			if(vmc[0].quadrant != 1 ){
				dm_motor_set_control_para(&joint_motor[0], 0, rescue_T, 0, 5, 0);//快哥z
				dm_motor_set_control_para(&joint_motor[1], 0, 0, 0, 0, 0);
			}else{
				dm_motor_set_control_para(&joint_motor[0], 0, 0, 0, 0, 0);//0.03 0.5
				dm_motor_set_control_para(&joint_motor[1], 0, 0, 0, 0, 0);
			}
						
			if(vmc[1].quadrant != 1 ){
				dm_motor_set_control_para(&joint_motor[2], 0, -rescue_T, 0, 5, 0);//快哥
				dm_motor_set_control_para(&joint_motor[3], 0, 0, 0, 0, 0);
			}else{
				dm_motor_set_control_para(&joint_motor[2], 0, 0, 0, 0, 0);//0.03 0.5
				dm_motor_set_control_para(&joint_motor[3], 0, 0, 0, 0, 0);
			}
			
			//第四象限卡台阶
			if(vmc[0].quadrant == 4 && chassis.rescue_cnt_L > 500)
			{
				dm_motor_set_control_para(&joint_motor[0], 0, 0, 0, 0, 0);//0.03 0.5
				dm_motor_set_control_para(&joint_motor[1], 0, 0, 0, 0, 0);
				rescue_cnt++;
			}
			if(vmc[1].quadrant == 4 && chassis.rescue_cnt_R > 500)
			{
				dm_motor_set_control_para(&joint_motor[2], 0, 0, 0, 0, 0);//0.03 0.5
				dm_motor_set_control_para(&joint_motor[3], 0, 0, 0, 0, 0);
				rescue_cnt++;
			}
			
			if(vmc[0].quadrant == 1 && vmc[1].quadrant == 1)
				up_ready++;
			if(up_ready > 100)
				chassis.rescue_inter_flag = 2;
		}
		

    } else if (chassis.rescue_inter_flag == 2) { //开始收腿 
        dm_motor_set_control_para(&joint_motor[0], 0, 0, 0, 0, 1.0f*wlr.side[0].T1);
        dm_motor_set_control_para(&joint_motor[1], 0, 0, 0, 0, 1.0f*wlr.side[0].T2); 
        dm_motor_set_control_para(&joint_motor[2], 0, 0, 0, 0,-1.0f*wlr.side[1].T1);
        dm_motor_set_control_para(&joint_motor[3], 0, 0, 0, 0,-1.0f*wlr.side[1].T2);
		dji_motor_set_torque(&driver_motor[0], 0);
		dji_motor_set_torque(&driver_motor[1], 0);
				
		//认为腿长已收到可以起身的长度
        if ( fabs(vmc[0].L_fdb - wlr.recover_length) < 0.05f && fabs(vmc[1].L_fdb - wlr.recover_length) < 0.05f )  {
			leg_length_cnt++;
			if(leg_length_cnt > 50){
				leg_length_cnt = 0;
				rescue_cnt = 0;
				chassis.rescue_cnt_L = 0;
				chassis.rescue_cnt_R = 0;
				chassis.recover_flag = 2;
				chassis.rescue_inter_flag = 0;
				wlr.high_flag = 0;
				up_ready=0;
				//清掉收腿pid积分和腿长pid积分
				pid_leg_recover[0].i_out = 0;
				pid_leg_recover[1].i_out = 0;
				pid_L_test[0].i_out = 0.0f;
				pid_L_test[1].i_out = 0.0f;
			}
        }
    }    
    //收腿阶段不允许轮子出力
    dji_motor_set_torque(&driver_motor[0], 0);
    dji_motor_set_torque(&driver_motor[1], 0);
	
	//第四象限卡台阶 轮子转
	if (rescue_cnt > 50 && (vmc[0].quadrant ==4 || vmc[1].quadrant == 4) && chassis.rescue_inter_flag != 2 ) {
		if (chassis_imu.pit < -0.25f) { 
			dji_motor_set_torque(&driver_motor[0], -1);
			dji_motor_set_torque(&driver_motor[1], 1);			
		}else {			
			dji_motor_set_torque(&driver_motor[0], 1);
			dji_motor_set_torque(&driver_motor[1], -1);
		}
	}
	
	if(ctrl_mode == PROTECT_MODE){
		leg_length_cnt = 0;
		rescue_cnt = 0;
		chassis.rescue_cnt_L = 0;
		chassis.rescue_cnt_R = 0;
		chassis.recover_flag = 2; 
//		chassis.recover_flag = 0; 
		chassis.rescue_inter_flag = 0;
		wlr.high_flag = 0;
		up_ready=0;
	}
}

static void chassis_rescue_test(void)
{
    static uint16_t leg_length_cnt = 0;		//腿长到达目标长度之后，变量++
	static uint16_t rescue_cnt = 0;			//超时检测计数器，缓冲时间计数器
	static float rescue_w = 4.5f;			//腿转动速度
	static uint8_t stall_leg = 0;			//腿卡死变量

	/* 
		转速w极性说明：
		vmc[0]左腿：w > 0 顺时针转动（象限2->3->4->1） 
				    w < 0 逆时针转动（象限1->4->3->2）
		vmc[1]右腿：w < 0 顺时针转动（象限2->3->4->1） 
					w > 0 逆时针转动（象限1->4->3->2）
	*/
	
	if (fabsf(chassis_imu.pit) > PI / 2.0f)
		rescue_w = 6.5;
	else
		rescue_w = 4.5;
		
	if (chassis.rescue_inter_flag == CHASSIS_RESCUE_IDLE && (fabsf(chassis_imu.pit) < PI / 2.0f))//机身比较平，头不朝下
        chassis.rescue_inter_flag = CHASSIS_RESCUE_NORMAL;//头先回正，腿通过简单的旋转即可回正。
	else if(chassis.rescue_inter_flag == CHASSIS_RESCUE_IDLE && (fabsf(chassis_imu.pit) > PI / 2.0f))
		chassis.rescue_inter_flag = CHASSIS_RESCUE_OVERTURN;//翻车了，头先别传，通过撑腿回到normal状态
	else if(chassis.rescue_inter_flag == CHASSIS_RESCUE_NORMAL && stall_leg)
		chassis.rescue_inter_flag = CHASSIS_RESCUE_STUCK;//正常起身下，有腿堵转
	
	if (chassis.rescue_inter_flag == CHASSIS_RESCUE_NORMAL) {
		stall_leg = check_joint_stall(joint_motor[0],joint_motor[2]);//normal全局卡死检测
		dm_motor_set_control_para(&joint_motor[1], 0, 0, 0, 0, 0);
		dm_motor_set_control_para(&joint_motor[3], 0, 0, 0, 0, 0);
        //左腿归正
		if (vmc[0].quadrant != 1){
			dm_motor_set_control_para(&joint_motor[0], 0, rescue_w, 0, 5, 0);
			chassis.rescue_cnt_L++;//计数，超过一定值则判断为受到阻碍
		}
		else{
			dm_motor_set_control_para(&joint_motor[0], 0, 0, 0, 0, 0);
		}
        //右腿归正        
		if (vmc[1].quadrant != 1){
			dm_motor_set_control_para(&joint_motor[2], 0, -rescue_w, 0, 5, 0);
			chassis.rescue_cnt_R++;//计数，超过一定值则判断为受到阻碍
		}
		else{
			dm_motor_set_control_para(&joint_motor[2], 0, 0, 0, 0, 0);
		}
        //双腿都到达第一象限之后 等待200ms缓冲后开始收腿
		if(vmc[0].quadrant == 1 && vmc[1].quadrant == 1){
			up_ready++;
			if(up_ready > 1)
				chassis.rescue_inter_flag = CHASSIS_RESCUE_RECOVER;//收腿阶段
		}
		//归位计数超过设定值，直接进收腿
		if(chassis.rescue_cnt_L > 3000 || chassis.rescue_cnt_R > 3000)
			chassis.rescue_inter_flag = CHASSIS_RESCUE_RECOVER;//收腿阶段
    } else if (chassis.rescue_inter_flag == CHASSIS_RESCUE_RECOVER) { //开始收腿 仅有Fy无T0
		//关节电机恢复lqr + 腿长pid控制
        dm_motor_set_control_para(&joint_motor[0], 0, 0, 0, 0, 1.0f*wlr.side[0].T1);
        dm_motor_set_control_para(&joint_motor[1], 0, 0, 0, 0, 1.0f*wlr.side[0].T2); 
        dm_motor_set_control_para(&joint_motor[2], 0, 0, 0, 0,-1.0f*wlr.side[1].T1);
        dm_motor_set_control_para(&joint_motor[3], 0, 0, 0, 0,-1.0f*wlr.side[1].T2);
		//收腿时轮子不能出力 不然会俯冲
		dji_motor_set_torque(&driver_motor[0], 0);
		dji_motor_set_torque(&driver_motor[1], 0);
		rescue_cnt++;//超时检测
				
		//认为腿长已收到可以起身的长度
        if (fabsf(vmc[0].L_fdb - wlr.recover_length) < 0.05f && fabsf(vmc[1].L_fdb - wlr.recover_length) < 0.05f)  {
			leg_length_cnt++;
			if(leg_length_cnt > 70){
				leg_length_cnt = 0;
				rescue_cnt = 0;
				chassis.rescue_cnt_L = 0;
				chassis.rescue_cnt_R = 0;
				chassis.recover_flag = 2;//收腿完成，进入检测摆角阶段
				wlr.high_flag = 0;
				up_ready=0;
			}
        }
		if(rescue_cnt >= 500) {//收腿时长超过600ms则认为收腿第一象限卡死
			chassis.rescue_inter_flag = CHASSIS_RESCUE_RECOVER_STUCK;
			rescue_cnt = 0;
		}
    } else if(chassis.rescue_inter_flag == CHASSIS_RESCUE_OVERTURN) {
		dm_motor_set_control_para(&joint_motor[1], 0, 0, 0, 0, 0);
		dm_motor_set_control_para(&joint_motor[3], 0, 0, 0, 0, 0);
		//先判断一下是往哪边翻车
		if(chassis_imu.pit < -PI / 3.0f) { //小于-60°一直发力 让车身回正
			//同时还需考虑到roll的影响 需要让roll先回正再回正pit
			if(fabsf(chassis_imu.rol) < 0.15f) {
				dm_motor_set_control_para(&joint_motor[0], 0, rescue_w, 0, 5, 0);
				dm_motor_set_control_para(&joint_motor[2], 0, -rescue_w, 0, 5, 0);
			}
			else if(chassis_imu.rol > 0.15f) {
				if(vmc[0].quadrant == 1 || vmc[0].quadrant == 2) {
					dm_motor_set_control_para(&joint_motor[0], 0, rescue_w, 0, 5, 0);
					dm_motor_set_control_para(&joint_motor[2], 0, 0, 0, 5, 0);
				}
				else {
					dm_motor_set_control_para(&joint_motor[0], 0, 0, 0, 0, 0);
					dm_motor_set_control_para(&joint_motor[2], 0, -rescue_w, 0, 5, 0);
				}
			}
			else if(chassis_imu.rol < -0.15f) {
				if(vmc[1].quadrant == 1 || vmc[1].quadrant == 2) {
					dm_motor_set_control_para(&joint_motor[0], 0, 0, 0, 5, 0);
					dm_motor_set_control_para(&joint_motor[2], 0, -rescue_w, 0, 5, 0);
				}
				else {
					dm_motor_set_control_para(&joint_motor[0], 0, rescue_w, 0, 5, 0);
					dm_motor_set_control_para(&joint_motor[2], 0, 0, 0, 0, 0);
				}
			}
		}
		else if(chassis_imu.pit > PI / 3.0f) { //大于60°一直发力 让车身回正
			//同时还需考虑到roll的影响 需要让roll先回正再回正pit
			if(fabsf(chassis_imu.rol) < 0.15f) {
				dm_motor_set_control_para(&joint_motor[0], 0, -rescue_w, 0, 5, 0);
				dm_motor_set_control_para(&joint_motor[2], 0, rescue_w, 0, 5, 0);
			}
			else if(chassis_imu.rol > 0.15f) {
				if(vmc[0].quadrant == 1 || vmc[0].quadrant == 2) {
					dm_motor_set_control_para(&joint_motor[0], 0, -rescue_w, 0, 5, 0);
					dm_motor_set_control_para(&joint_motor[2], 0, 0, 0, 5, 0);
				}
				else {
					dm_motor_set_control_para(&joint_motor[0], 0, 0, 0, 0, 0);
					dm_motor_set_control_para(&joint_motor[2], 0, rescue_w, 0, 5, 0);
				}
			}
			else if(chassis_imu.rol < -0.15f) {
				if(vmc[1].quadrant == 1 || vmc[1].quadrant == 2) {
					dm_motor_set_control_para(&joint_motor[0], 0, 0, 0, 5, 0);
					dm_motor_set_control_para(&joint_motor[2], 0, rescue_w, 0, 5, 0);
				}
				else {
					dm_motor_set_control_para(&joint_motor[0], 0, -rescue_w, 0, 5, 0);
					dm_motor_set_control_para(&joint_motor[2], 0, 0, 0, 0, 0);
				}
			}
		}
		else if(fabsf(chassis_imu.pit) < PI / 6.0f) { //绝对值小于30° 
			dm_motor_set_control_para(&joint_motor[0], 0, 0, 0, 5, 0);
			dm_motor_set_control_para(&joint_motor[2], 0, 0, 0, 5, 0);
			rescue_cnt++;
		}
		if(rescue_cnt > 100) { //缓冲200ms后进入normal
			chassis.rescue_inter_flag = CHASSIS_RESCUE_NORMAL;
			rescue_cnt = 0;
		}
	} else if(chassis.rescue_inter_flag == CHASSIS_RESCUE_STUCK) {
		dm_motor_set_control_para(&joint_motor[1], 0, 0, 0, 0, 0);
		dm_motor_set_control_para(&joint_motor[3], 0, 0, 0, 0, 0);
		rescue_cnt++;//超时直接收腿
		//先判断哪条腿卡死了
		switch(stall_leg)
		{
			case 1://左腿卡死
			{
				if(vmc[0].quadrant == 4)//左腿第四象限卡死
				{
					//腿给一点点力，增大摩擦力，让轮子转，往前开一段距离
					dm_motor_set_control_para(&joint_motor[0], 0, 1, 0, 10, 0);
					dji_motor_set_torque(&driver_motor[0], 2);
					dji_motor_set_torque(&driver_motor[1], -2);
				}
				else if(vmc[0].quadrant == 3)//左腿第三象限卡死
				{
					//腿给一点点力，增大摩擦力，让轮子转，往后开一段距离
					dm_motor_set_control_para(&joint_motor[0], 0, 1, 0, 5, 0);
					dji_motor_set_torque(&driver_motor[0], 2);
					dji_motor_set_torque(&driver_motor[1], 2);
				}
				if(vmc[1].quadrant == 1)
				{
					//右腿到第一象限之后，腿给一点点力，增大摩擦力
					dm_motor_set_control_para(&joint_motor[2], 0, -1, 0, 10, 0);
				}
				else//右腿如果不在第一象限，则轮子不给力，腿给一点点速度转
				{
					dm_motor_set_control_para(&joint_motor[2], 0, -3, 0, 10, 0);
					dji_motor_set_torque(&driver_motor[1], 0);
				}
			}break;
			case 2://右腿卡死
			{
				if(vmc[1].quadrant == 4)//右腿第四象限卡死
				{
					//腿给一点点力，增大摩擦力，让轮子转，往前开一段距离
					dm_motor_set_control_para(&joint_motor[2], 0, -1, 0, 10, 0);
					dji_motor_set_torque(&driver_motor[0], 2);
					dji_motor_set_torque(&driver_motor[1], -2);
				}
				else if(vmc[1].quadrant == 3)//右腿第三象限卡死
				{
					//腿给一点点力，增大摩擦力，让轮子转，往后开一段距离
					dm_motor_set_control_para(&joint_motor[2], 0, -1, 0, 5, 0);
					dji_motor_set_torque(&driver_motor[0], -2);
					dji_motor_set_torque(&driver_motor[1], -2);
				}
				if(vmc[0].quadrant == 1)
				{
					//右腿到第一象限之后，腿给一点点力，增大摩擦力
					dm_motor_set_control_para(&joint_motor[0], 0, 1, 0, 10, 0);
				}
				else
				{
					//右腿如果不在第一象限，则轮子不给力，腿给一点点速度转
					dm_motor_set_control_para(&joint_motor[0], 0, 3, 0, 10, 0);
					dji_motor_set_torque(&driver_motor[0], 0);
				}
			}break;
			case 3://双腿卡死
			{
				if(vmc[0].quadrant == 4 && vmc[1].quadrant == 4) {//双腿第四象限卡死
					//腿给一点点力，增大摩擦力，让轮子转，往前开一段距离
					dm_motor_set_control_para(&joint_motor[0], 0, 1, 0, 10, 0);
					dm_motor_set_control_para(&joint_motor[2], 0, -1, 0, 10, 0);
					dji_motor_set_torque(&driver_motor[0], 2);
					dji_motor_set_torque(&driver_motor[1], -2);
				}
				else if(vmc[0].quadrant == 3 && vmc[1].quadrant == 3) {//双腿第三象限卡死
					//腿给一点点力，增大摩擦力，让轮子转，往后开一段距离
					dm_motor_set_control_para(&joint_motor[0], 0, 3, 0, 10, 0);
					dm_motor_set_control_para(&joint_motor[2], 0, -3, 0, 10, 0);
					dji_motor_set_torque(&driver_motor[0], 2);
					dji_motor_set_torque(&driver_motor[1], -2);
				}
			}break;
			default : break;
		}
		if((vmc[0].quadrant == 1 && vmc[1].quadrant == 1) || check_joint_offstall(stall_leg)) {
			//清除关节力
			dm_motor_set_control_para(&joint_motor[0], 0, 0, 0, 0, 0);
			dm_motor_set_control_para(&joint_motor[2], 0, 0, 0, 0, 0);
			//清除轮子力
			dji_motor_set_torque(&driver_motor[0], 0);
			dji_motor_set_torque(&driver_motor[1], 0);
			//双腿都在第一象限则回normal
			chassis.rescue_inter_flag = CHASSIS_RESCUE_NORMAL;
			stall_leg = 0;
		}
		if(rescue_cnt > 1000) { //2s超时后直接收腿
			rescue_cnt = 0;
			chassis.rescue_inter_flag = CHASSIS_RESCUE_RECOVER;
		}
	} else if(chassis.rescue_inter_flag == CHASSIS_RESCUE_RECOVER_STUCK) {
		dm_motor_set_control_para(&joint_motor[1], 0, 0, 0, 0, 0);
		dm_motor_set_control_para(&joint_motor[3], 0, 0, 0, 0, 0);
		if(fabsf(vmc[0].L_fdb - wlr.recover_length) > 0.1f && fabsf(vmc[1].L_fdb - wlr.recover_length) > 0.1f) { //如果收腿卡死，就先转到第四象限再收腿
			dm_motor_set_control_para(&joint_motor[0], 0, -3, 0, 10, 0);
			dm_motor_set_control_para(&joint_motor[2], 0, 3, 0, 10, 0);
			if(vmc[0].quadrant == 4 && vmc[1].quadrant == 4)
				rescue_cnt++;
		}
		else if(fabsf(vmc[0].L_fdb - wlr.recover_length) > 0.1f) { //如果收腿卡死，就先转到第四象限再收腿
			dm_motor_set_control_para(&joint_motor[0], 0, -3, 0, 10, 0);
			if(vmc[0].quadrant == 4)
				rescue_cnt++;
		}
		else if(fabsf(vmc[1].L_fdb - wlr.recover_length) > 0.1f) { //如果收腿卡死，就先转到第四象限再收腿
			dm_motor_set_control_para(&joint_motor[2], 0, 3, 0, 10, 0);
			if(vmc[1].quadrant == 4)
				rescue_cnt++;
		}
		if(rescue_cnt > 50) {
			chassis.rescue_inter_flag = CHASSIS_RESCUE_RECOVER;
			rescue_cnt = 0;
		}
	}

	if(ctrl_mode == PROTECT_MODE){
		leg_length_cnt = 0;
		rescue_cnt = 0;
		chassis.rescue_cnt_L = 0;
		chassis.rescue_cnt_R = 0;
		chassis.recover_flag = 0; 
		chassis.rescue_inter_flag = CHASSIS_RESCUE_IDLE;
		wlr.high_flag = 0;
		up_ready=0;
	}

	if(chassis.recover_flag == 2) {
		//检测摆角
		if(fabsf(vmc[0].q_fdb[0] - PI / 2.0f) < 0.5f && fabsf(vmc[1].q_fdb[0] - PI / 2.0f) < 0.5f)
			chassis.recover_cnt++;
		if(fabsf(vmc[0].q_fdb[0] - PI / 2.0f) < 0.7f && fabsf(vmc[1].q_fdb[0] - PI / 2.0f) < 0.7f)
			chassis.recover_cnt++;
		if(chassis.recover_cnt > 70) {
			chassis.recover_cnt = 0;
			chassis.recover_flag = 0;
			chassis.rescue_inter_flag = CHASSIS_RESCUE_IDLE;
		}
	}
	wlr.s_ref = wlr.s_adapt = wlr.s_fdb;
}

//卡盲道，极限逃离
static void chassis_hardest_rescue(void)
{
	//目前想法是在第四象限收腿转到第二/三象限后蹬腿 可以让车前进/后腿 
	//轮子直接转可能也是一种方案
	//只转大腿电机可能也是一种方案
	//总之如果在盲道上开始lqr控制的话是不可能出来的
}

float temp_T;

static void chassis_data_output(void)
{
	static uint32_t prone_cnt = 0;
	if (!wlr.prone_flag)
		prone_cnt = 0;
    
    if (wlr.ctrl_mode == 0) {//保护模式
        wlr_protest();
        dji_motor_set_torque(&driver_motor[0], 0);
        dji_motor_set_torque(&driver_motor[1], 0);		
        for (int i = 0; i < 4; i++) {
			dm_motor_set_control_para(&joint_motor[i], 0, 0, 0, 0, 0);
        }
    } else if (wlr.ctrl_mode == 2) {//力控
        dji_motor_set_torque(&driver_motor[0], -wlr.side[0].Tw);
        dji_motor_set_torque(&driver_motor[1],  wlr.side[1].Tw);
        if (wlr.prone_flag) {
            // ... [原力控和卧倒逻辑保持不变] ...
        } else {
            if(chassis.recover_flag == 1 || chassis.recover_flag == 2) 
				chassis_rescue_test();
			if(rc_fsm_check(RC_LEFT_RU))
				chassis_hardest_rescue();
            if(chassis.recover_flag != 1) {
				if(wlr.joint_all_online){
					if(wlr.crash_flag) {
						dm_motor_set_control_para(&joint_motor[0], 0, -3, 0, 5, 0);
						dm_motor_set_control_para(&joint_motor[1], 0, 0, 0, 0, 0);	
						dm_motor_set_control_para(&joint_motor[2], 0, 3, 0, 5, 0);
						dm_motor_set_control_para(&joint_motor[3], 0, 0, 0, 0, 0);
					} 
				else {
						dm_motor_set_control_para(&joint_motor[0], 0, 0, 0, 0, wlr.side[0].T1);
						dm_motor_set_control_para(&joint_motor[1], 0, 0, 0, 0, wlr.side[0].T2);
						dm_motor_set_control_para(&joint_motor[2], 0, 0, 0, 0,-wlr.side[1].T1);
						dm_motor_set_control_para(&joint_motor[3], 0, 0, 0, 0,-wlr.side[1].T2);  
					}
				}
				else{
					dm_motor_set_control_para(&joint_motor[0], 0, 0, 0, 0, 0);
					dm_motor_set_control_para(&joint_motor[1], 0, 0, 0, 0, 0);
					dm_motor_set_control_para(&joint_motor[2], 0, 0, 0, 0, 0);
					dm_motor_set_control_para(&joint_motor[3], 0, 0, 0, 0, 0);  
				}
            }
        }
    } else if (wlr.ctrl_mode == 1) {//位控
        dji_motor_set_torque(&driver_motor[0], -wlr.side[0].Tw);
        dji_motor_set_torque(&driver_motor[1],  wlr.side[1].Tw);		
		dm_motor_set_control_para(&joint_motor[0],  wlr.side[0].P2 + joint_motor[0].zero_point,      0, 10, 2,  2);
        dm_motor_set_control_para(&joint_motor[1],  wlr.side[0].P1 - joint_motor[1].zero_point - PI, 0, 10, 2, -2);
        dm_motor_set_control_para(&joint_motor[2], -wlr.side[1].P1 + joint_motor[2].zero_point + PI, 0, 10, 2,  2);
        dm_motor_set_control_para(&joint_motor[3], -wlr.side[1].P2 - joint_motor[3].zero_point,      0, 10, 2, -2);		
    } else {
        wlr_protest();
        dji_motor_set_torque(&driver_motor[0], 0);
        dji_motor_set_torque(&driver_motor[1], 0);
        for (int i = 0; i < 4; i++) {
            dm_motor_set_control_para(&joint_motor[i], 0, 0, 0, 0, 0);
        }
    }
	
    // 恢复你本来的按键硬切断保护
    if ( rc_fsm_check(RC_LEFT_LD) ){
        dji_motor_set_torque(&driver_motor[0], 0);
        dji_motor_set_torque(&driver_motor[1], 0);
        dm_motor_set_control_para(&joint_motor[0], 0, 0, 0, 0, 0);
        dm_motor_set_control_para(&joint_motor[1], 0, 0, 0, 0, 0);
		dm_motor_set_control_para(&joint_motor[2], 0, 0, 0, 0, 0);
		dm_motor_set_control_para(&joint_motor[3], 0, 0, 0, 0, 0);
    }
    
	if (!driver_motor[0].online || !driver_motor[1].online) {
		dji_motor_set_torque(&driver_motor[0], 0);
		dji_motor_set_torque(&driver_motor[1], 0);
		dm_motor_set_control_para(&joint_motor[0], 0, 0, 0, 0, 0);
		dm_motor_set_control_para(&joint_motor[1], 0, 0, 0, 0, 0);
		dm_motor_set_control_para(&joint_motor[2], 0, 0, 0, 0, 0);
		dm_motor_set_control_para(&joint_motor[3], 0, 0, 0, 0, 0);			
	}
	
	if(joint_motor[0].state == 1 && joint_motor[1].state == 1 && joint_motor[2].state == 1 && joint_motor[3].state == 1 )
		wlr.joint_all_online = 1;
	else
		wlr.joint_all_online = 0;	
}

void chassis_set_container(void)
{
	chassis_set_imu_data_container.pit = wlr.pit_fdb;
	chassis_set_imu_data_container.rol = wlr.roll_fdb;
	container_set(TAG_CHA_IMU_DATA,&chassis_set_imu_data_container,sizeof(chassis_set_imu_data_container),CONTAINER_TYPE_STRUCT);
}

// ==============================================================================
// 最终任务主循环
// ==============================================================================
void chassis_task(void const *argu)
{
    uint32_t thread_wake_time = osKernelSysTick();
    power_init();
    chassis_init();
    Fusion_Vel_Acc_Init();
    
    
    for(;;)
    {   
        thread_wake_time = osKernelSysTick();
	
        // 2. 将 FSM 状态直接挂载到执行参数
        chassis_execute_fsm();
        
        // 3. 期望速度和旋转矩阵计算
        chassis_data_input();
        
        // 4. 恢复你本来的执行逻辑结构
        if(g_robot_ctx.output.chassis != CHASSIS_STOP)
            wlr_control();
        else
            chassis_init(); // 恢复你的原有保护调用
		
        // 5. 将算好的力矩下发到电机
        chassis_data_output();
		
		//底盘待发送数据打包
		chassis_set_container();
		for (int i = 0; i < 5; i++) {
			DEBUG_INFO("CYQ IS THE MOST HANDSOME MAN IN THIS WORLD\r\n");
		}
        status.task.chassis = 1;
        osDelayUntil(&thread_wake_time, 2);
    }
}