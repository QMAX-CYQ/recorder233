#include "mode_switch_task.h"
#include "status_task.h"
#include "control_def.h"
#include "drv_dji_motor.h"
#include "prot_vision.h"
#include "prot_dr16.h"
#include "prot_imu.h"
#include "math_lib.h"
#include "cmsis_os.h"
#include "arm_math.h"
#include "string.h"
#include "func_generator.h"
#include "gimbal_task.h"
#include "wlr.h"
#include "chassis_task.h"
#include "status_task.h"
#include "board_comm.h"
#include "container.h"
#include "log.h"
float yaw_err_up;
FGT_agl_t yaw_test = {
    .Td = 1,
    .time = 0,
    .T1 = 150,
    .T2 = 0,
    .T1_out = 0,
    .T2_out = 0.175,
    .out = 0
};

gimbal_scale_t gimbal_scale = {
    .ecd_remote = 0.000005f,//待修改sensity
    .ecd_keyboard = 1,
    .angle_remote = 0.000007f,
    .angle_keyboard = 0.00003f
};
gimbal_t gimbal;
gimbal_stable_t gimbal_stable;

extern gimbal_data_t gimbal_data_rec;

static gimbal_data_t gimbal_get_gimbal_data_container;
static vision_data_t gimbal_get_vision_data_container;

static gimbal_tx_data_t gimbal_set_gimbal_data_container;



// 收到gimbal数据：
static void gimbal_data_cb(uint32_t tag_id, void* data, size_t len) {
	if(data == NULL || len != sizeof(gimbal_data_t))
		return;
	memcpy(&gimbal_get_gimbal_data_container,(gimbal_data_t*)data,len);
}

// 收到vision数据：
static void vision_data_cb(uint32_t tag_id, void* data, size_t len) {
	if(data == NULL || len != sizeof(vision_data_t))
		return;
    memcpy(&gimbal_get_vision_data_container,(vision_data_t*)data,len);
}



// --- 回调配置表  ---
static const ContainerBusCfg mb_callback[] = {
    { TAG_GIMBAL_OUTPUT_DATA, gimbal_data_cb, NULL },
    { TAG_TRACE_VISION_DATA, vision_data_cb, NULL },
};





static void gimbal_init(void)
{
    memset(&gimbal, 0, sizeof(gimbal_t));
	
	pid_init(&gimbal.pit_angle.pid, CHANG_I_RATE, 22.0f, 0.5f, 0, 25, 50);//17 0 0 
    pid_init(&gimbal.pit_spd.pid, CHANG_I_RATE, 1.0f, 0.165f, 0, 2.5f, 7.0f);//0.52 0.165 
	gimbal.pit_angle.pid.threshold_a = 0.0f;
	gimbal.pit_angle.pid.threshold_b = 0.0f;
	
	pid_init(&gimbal.yaw_angle.pid, CHANG_I_RATE,25.0f, 0.1f, 0.0f, 50, 100);//尝试云台补偿算法
    pid_init(&gimbal.yaw_spd.pid, CHANG_I_RATE, 8000.0f, 0.00f, 0, 10000.0f, 25000.0f);
	gimbal.yaw_angle.pid.threshold_a = 0.002f;
	gimbal.yaw_angle.pid.threshold_b = 0.3f; 
	
	container_bus_init(mb_callback, sizeof(mb_callback)/sizeof(ContainerBusCfg));
	
//    pid_init(&gimbal.yaw_ecd.pid, NONE, 10.0f, 0, 0, 0.0f, 30.0f);
//	pid_init(&gimbal.yaw_spd_ecd.pid, NONE, 6000.0f, 10.00f, 0, 1000.0f, 25000.0f);
}

float yaw_err;
float slope_feed,last_yaw_ref,slope_feed_pit,last_pit_ref;
static void gimbal_pid_calc(void)
{
    float  pit_max, pit_min;
    //位置环反馈 陀螺仪 -0.6 0.4
    //速度环反馈 陀螺仪
    //此yaw_err用于云台pit限幅
    yaw_err = circle_error(CHASSIS_YAW_OFFSET / 8192.0f * 2 * PI, yaw_motor.ecd / 8192.0f * 2 * PI, 2 * PI);
//    pit_max = -arm_cos_f32(yaw_err) * chassis_imu.pit + 0.50f;
//		//1111 测试架，pit没装好，暂时限0.38
//	//pit_max = -arm_cos_f32(yaw_err) * chassis_imu.pit + 0.32f;
//    pit_min = -arm_cos_f32(yaw_err) * chassis_imu.pit - 0.35f;
	
	pit_max = 0.62f;
	pit_min = -0.44f;
    data_limit(&gimbal.pit_angle.ref, pit_min, pit_max);
    gimbal.pit_angle.fdb = gimbal_imu.pit;
		
	slope_feed_pit = gimbal.pit_angle.ref - last_pit_ref;
	
//		gimbal.pit_angle.ref = gimbal.pit_angle.ref + (-gimbal.feedback_beta_speed_input * 0.002f);//云台稳定算法

    gimbal.pit_spd.ref = pid_calc(&gimbal.pit_angle.pid, gimbal.pit_angle.ref - 0.0f * 0.002f + slope_feed_pit , gimbal.pit_angle.fdb);
//    gimbal.pit_spd.fdb = -gimbal_imu.wy - arm_cos_f32(yaw_err) * chassis_imu.wy;
    gimbal.pit_spd.fdb = gimbal_imu.wy;
    gimbal.pit_output = -pid_calc(&gimbal.pit_spd.pid, gimbal.pit_spd.ref, gimbal.pit_spd.fdb);

    //pid参数选择

    
    if (gimbal.yaw_angle.ref < 0) {
        gimbal.yaw_angle.ref += 2 * PI;
    } else if (gimbal.yaw_angle.ref > 2 * PI) {
        gimbal.yaw_angle.ref -= 2 * PI;
    }
    //视觉测试
//    gimbal.yaw_angle.ref = FGT_agl_calc(&yaw_test);
    gimbal.yaw_angle.fdb = gimbal_imu.yaw;
    //此yaw_err用于云台yaw环形控制
		

//	slope_feed = gimbal.yaw_angle.ref - last_yaw_ref;

	slope_feed = 0;
		
    yaw_err = circle_error(gimbal.yaw_angle.ref + 0.000f + slope_feed  , gimbal.yaw_angle.fdb, 2*PI);
    if (gimbal.start_up == 0 && fabsf(yaw_err) < 0.03f)//-------------------->无云台控制下注释
        gimbal.start_up = 1;
	
    gimbal.yaw_spd.ref = pid_calc(&gimbal.yaw_angle.pid, gimbal.yaw_angle.fdb + yaw_err, gimbal.yaw_angle.fdb);    
    gimbal.yaw_spd.fdb = gimbal_imu.wz;
    gimbal.yaw_output = pid_calc(&gimbal.yaw_spd.pid, gimbal.yaw_spd.ref, gimbal.yaw_spd.fdb);//SMC
	//起身先转pitch
//	if((!(fabsf(gimbal.pit_angle.ref - gimbal.pit_angle.fdb) < 0.03f) && !gimbal.start_up) && gimbal.start_cnt < 200)
//		gimbal.yaw_output = 0.0f;
		
//		if (gimbal.start_up == 0 && fabs(yaw_err)  > 0.03f)//-------------------->无云台控制下注释
//       gimbal.yaw_output  *= 0.1f;
    
//    //无云台控制------->无陀螺仪, 通过ecd控制
//	if( rc_fsm_check(RC_LEFT_LD) && (!(rc_fsm_check(RC_RIGHT_RD))))
//	{  //  	调弹道
//		gimbal.yaw_ecd.fdb = (float)yaw_motor.ecd / 8192 * 2  * PI;
//		yaw_err = circle_error(gimbal.yaw_ecd.ref, gimbal.yaw_ecd.fdb, 2*PI);
//		gimbal.yaw_spd_ecd.ref = pid_calc(&gimbal.yaw_ecd.pid, gimbal.yaw_ecd.fdb + yaw_err, gimbal.yaw_ecd.fdb);
//			
//		gimbal.yaw_spd_ecd.fdb = gimbal_imu.wz;
//		gimbal.yaw_output = pid_calc(&gimbal.yaw_spd_ecd.pid, gimbal.yaw_spd_ecd.ref, gimbal.yaw_spd_ecd.fdb);
//	}
		
	last_yaw_ref = gimbal.yaw_angle.ref;
	last_pit_ref = gimbal.pit_angle.ref;
//		    if (gimbal.start_up == 0 && yaw_err < 0.03f) //无云台时起身判断
//        gimbal.start_up = 1;    

}

static void gimbal_data_output(void)
{
//    dji_motor_set_torque(&pit_motor, gimbal.pit_output);
    dji_motor_set_torque(&yaw_motor, 1.0f*gimbal.yaw_output);   
}

static void gimbal_get_vision_data(void)
{
    switch (vision.aim_status) {
        case AIMING: {//识别到目标
            if (vision.new_frame_flag) {
                vision.new_frame_flag = 0;
                gimbal.pit_angle.ref = vision.target_pit_angle;
                gimbal.yaw_angle.ref = vision.target_yaw_angle;
            }
            break;
        }
        case FIRST_LOST: {//首次丢失
            vision.aim_status = UNAIMING;
            gimbal.pit_angle.ref = gimbal.pit_angle.fdb;
            gimbal.yaw_angle.ref = gimbal.yaw_angle.fdb;
            break;
        }
        case UNAIMING: {//未识别到目标
            if (ctrl_mode == REMOTER_MODE) {
                gimbal.pit_angle.ref -= rc.ch2 * gimbal_scale.angle_remote;
                gimbal.yaw_angle.ref -= rc.ch1 * gimbal_scale.angle_remote;
            } else if (ctrl_mode == KEYBOARD_MODE) {
                gimbal.pit_angle.ref += rc.mouse.y * gimbal_scale.angle_keyboard * 0.5f;
                gimbal.yaw_angle.ref -= rc.mouse.x * gimbal_scale.angle_keyboard;
            }
            break;
        }
        default: break;
    }
}

static void gimbal_stable_calc(void)
{
	gimbal_stable.chassis_wx_fdb = -chassis_imu.wx;
	gimbal_stable.chassis_wy_fdb = chassis_imu.wy;
	gimbal_stable.chassis_wz_fdb = chassis_imu.wz;
	
	gimbal_stable.a_fdb =  circle_error((float)CHASSIS_YAW_OFFSET / 8192 * 2 * PI, (float)yaw_motor.ecd / 8192 * 2 * PI, 2 * PI);
	if(gimbal_stable.a_fdb < 0.0f)
		gimbal_stable.a_fdb += 2 * PI;
	
	 if (g_robot_ctx.output.chassis  == CHASSIS_LOW_SPIN) 
		gimbal_stable.feedback_alpha_speed = -gimbal_stable.chassis_wz_fdb;
	else
		gimbal_stable.feedback_alpha_speed = 0;
	
	if(wlr.yaw_ref == (float)yaw_motor.ecd / 8192 * 2  * PI)
		gimbal_stable.feedback_beta_speed = arm_sin_f32(gimbal_stable.a_fdb) * gimbal_stable.chassis_wx_fdb - arm_cos_f32(gimbal_stable.a_fdb) * gimbal_stable.chassis_wy_fdb ;
	else
		gimbal_stable.feedback_beta_speed = arm_sin_f32(gimbal_stable.a_fdb) * (-gimbal_stable.chassis_wx_fdb) - arm_cos_f32(gimbal_stable.a_fdb) * (-gimbal_stable.chassis_wy_fdb);
}

static void yaw_control(void)
{
	 float yaw_err;
	if(ctrl_mode == PROTECT_MODE)
	{
		gimbal.yaw_angle.ref = CHASSIS_YAW_OFFSET / 8192.0f * 2.0f * PI;
		gimbal.yaw_spd.pid.i_out = 0;
		gimbal.yaw_ecd.pid.i_out = 0;
		gimbal.yaw_output = 0;
	}else{
		yaw_err = circle_error(gimbal.yaw_angle.ref, gimbal.yaw_angle.fdb, 2*PI);
		if (gimbal.start_up == 0 && yaw_err < 0.03f)
			gimbal.start_up = 1;
		
		gimbal.yaw_angle.fdb = yaw_motor.ecd / 8192.0f * 2.0f * PI;
		yaw_err = circle_error(gimbal.yaw_angle.ref, gimbal.yaw_angle.fdb, 2*PI);
		
		if (gimbal.yaw_angle.ref < 0) {
			gimbal.yaw_angle.ref += 2 * PI;
		} else if (gimbal.yaw_angle.ref > 2 * PI) {
			gimbal.yaw_angle.ref -= 2 * PI;
		}
		
		gimbal.yaw_spd.ref = pid_calc(&gimbal.yaw_angle.pid, gimbal.yaw_angle.fdb + yaw_err, gimbal.yaw_angle.fdb);    
		gimbal.yaw_spd.fdb = yaw_motor.velocity;
		gimbal.yaw_output = pid_calc(&gimbal.yaw_spd.pid, gimbal.yaw_spd.ref, gimbal.yaw_spd.fdb);
	}
}

static void gimbal_execute_fsm(void)
{
	gimbal.start_up = gimbal_get_gimbal_data_container.gimbal_start_up;
	switch(g_robot_ctx.output.gimbal)
	{
		case GIMBAL_STOP:
		{
			gimbal.yaw_output = gimbal_data_rec.yaw_output;
		}break;
		//听云台算完发下来的电流
		case GIMBAL_GYRO_STABILIZE:
		case GIMBAL_MOUSE_CONTROL:
		case GIMBAL_AUTO_AIM:
		{
			gimbal.yaw_output = gimbal_data_rec.yaw_output;
		}break;
		default : break;
	}
}

void gimbal_set_container(void)
{
	if(g_robot_ctx.output.chassis == CHASSIS_LOW_SPIN)
		gimbal_set_gimbal_data_container.feedback_alpha_speed_input = -chassis_imu.wz;
	else
		gimbal_set_gimbal_data_container.feedback_alpha_speed_input = 0.0f;
	gimbal_set_gimbal_data_container.feedback_beta_speed_input = 0.0F;
//	memcpy(gimbal_set_gimbal_data_container.yaw_raw_data,yaw_raw_data,8);
	container_set(TAG_GIMBAL_CTRL_DATA,&gimbal_set_gimbal_data_container,sizeof(gimbal_set_gimbal_data_container),CONTAINER_TYPE_STRUCT);	
}

void gimbal_task(void const *argu)
{
    uint32_t thread_wake_time = osKernelSysTick();
    gimbal_init();
    for(;;) {
        thread_wake_time = osKernelSysTick();
		
		gimbal_execute_fsm();
		
		gimbal_stable_calc();
		
		gimbal_data_output();
		
		gimbal_set_container();
		
		//help 拆头 + 了下面两个函数     不拆头就不加
//		yaw_control();
//		gimbal_data_output();
//				 for (int i = 0; i < 5; i++) {
//				DEBUG_INFO("ZGH IS THE MOST HANDSOME MAN IN THIS WORLD\r\n");
//		 }

        osDelayUntil(&thread_wake_time, 2);
    }
}
