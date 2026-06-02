#include "shoot_task.h"
#include "mode_switch_task.h"
#include "control_def.h"
#include "drv_dji_motor.h"
#include "drv_lk_motor.h"
#include "prot_judge.h"
#include "prot_dr16.h"
#include "prot_vision.h"
#include "data_buffer.h"
#include "cmsis_os.h"
#include "status_task.h"
#include "math_lib.h"
#include "board_comm.h"
#include "robot_logic.h"
#include "mode_switch_task.h"
#include "container.h"
#include "wlr.h"



#define SHOOT_SPEED_NUM 15
#ifndef ABS
    #define ABS(x) ((x>0)? (x): (-(x)))//32818
#endif

#ifdef DJI2006
    #define TRIGGER_MOTOR_ECD_SINGLE   (58975.0f)  //拨盘一颗子弹转过的编码值 8191 * 36 *2/10 = 58975.2f
    #define TRIGGER_MOTOR_ECD_SERIES   (58975.0f)  //拨盘一颗子弹转过的编码值 8191 * 36 *2/10 = 58975.2f
    #define TRIGGER_MOTOR_STUCK_CURRENT 7000	   //拨盘卡弹电流阈值 0~16384  
    #define TRIGGER_MOTOR_STUCK_SPEED   100	       //拨盘卡弹电流阈值 
#endif

#ifdef MG4005
    #define	TRIGGER_MOTOR_ECD_SINGLE    (65536.0f)  //拨盘一颗子弹转过的编码值 65536 * 10 / 10 = 65536.0f
    #define TRIGGER_MOTOR_ECD_SERIES    (65536.0f)	//拨盘一颗子弹转过的编码值 65536 * 10 / 10 = 65536.0f
    #define TRIGGER_MOTOR_STUCK_CURRENT 800	    //拨盘卡弹电流阈值 0~2048
    #define TRIGGER_MOTOR_STUCK_SPEED   1500	    //拨盘卡弹转速阈值  
#endif

#define SHOOT_DELAY_QUEUE_SIZE      8
#define SHOOT_DELAY_TIMEOUT_MS      500u
#define SHOOT_SPEED_CHANGE_EPS      (1e-3f)

static float Heat_ShootPeriod_calc(uint8_t trice_id);

float MIN_HEAT = 40;        //热量控制裕量

static uint16_t frequency_cnt = 0;	//射击周期计算
static uint8_t  shoot_enable  = 1;  //单发使能标志
float trigger_ecd_error;

//用于退蛋反转
uint32_t back_cnt = 0;
uint32_t err_cnt  = 0;
uint8_t back_flag = 0;

shoot_t shoot;
//static buffer_t *shoot_speed_buffer;

float vision_send_time;
float shoot_delay_time;          // latest raw delay from trigger command to judge shoot speed update
float last_shoot_speed;
uint32_t shoot_delay_update_cnt;
uint32_t shoot_delay_overflow_cnt;
uint32_t shoot_delay_timeout_cnt;

typedef struct
{
    uint32_t start_tick;
} shoot_delay_pending_t;

static shoot_delay_pending_t shoot_delay_queue[SHOOT_DELAY_QUEUE_SIZE];
static uint8_t shoot_delay_head;
static uint8_t shoot_delay_tail;
static uint8_t shoot_delay_cnt;

static void shoot_delay_pop(void)
{
    if (shoot_delay_cnt == 0) {
        return;
    }

    shoot_delay_head++;
    if (shoot_delay_head >= SHOOT_DELAY_QUEUE_SIZE) {
        shoot_delay_head = 0;
    }
    shoot_delay_cnt--;
}

static void shoot_delay_reset(void)
{
    shoot_delay_head = 0;
    shoot_delay_tail = 0;
    shoot_delay_cnt = 0;
    shoot_delay_time = 0.0f;
    vision_send_time = 0.0f;
    last_shoot_speed = shoot_data.initial_speed;
    shoot_delay_update_cnt = 0;
    shoot_delay_overflow_cnt = 0;
    shoot_delay_timeout_cnt = 0;
}

static void shoot_delay_record_fire(void)
{
    if (shoot_delay_cnt == 0) {
        last_shoot_speed = shoot_data.initial_speed;
    }

    if (shoot_delay_cnt >= SHOOT_DELAY_QUEUE_SIZE) {
        shoot_delay_pop();
        shoot_delay_overflow_cnt++;
    }

    shoot_delay_queue[shoot_delay_tail].start_tick = osKernelSysTick();
    shoot_delay_tail++;
    if (shoot_delay_tail >= SHOOT_DELAY_QUEUE_SIZE) {
        shoot_delay_tail = 0;
    }
    shoot_delay_cnt++;
    shoot.barrel.shoot_cnt++;
}

static void shoot_delay_drop_timeout(uint32_t now_tick)
{
    while (shoot_delay_cnt > 0) {
        uint32_t wait_tick = now_tick - shoot_delay_queue[shoot_delay_head].start_tick;
        if (wait_tick <= SHOOT_DELAY_TIMEOUT_MS) {
            break;
        }
        shoot_delay_pop();
        shoot_delay_timeout_cnt++;
    }
}

uint8_t global_back_flag;
//**********************添加预制弹位*************************//

uint8_t microcurrent_flag = 0;		//预制标志位，如果暂时不需要用直接改成2就行了
float micro_t = 0;					//微电流力矩
float micro_t_test = 0;				//测试用

vision_data_t shoot_get_vision_data_container;

static vision_tx_data_t shoot_set_vision_data_container;

// 收到vision数据：
static void vision_data_cb(uint32_t tag_id, void* data, size_t len) {
	if(data == NULL || len != sizeof(vision_data_t))
	    return;
    memcpy(&shoot_get_vision_data_container,(vision_data_t*)data,len);
	
}

// --- 回调配置表  ---
static const ContainerBusCfg mb_callback[] = {
    { TAG_TRACE_VISION_DATA, vision_data_cb, NULL }
};

void shoot_set_container(void)
{
	shoot_set_vision_data_container.shoot_speed = shoot_data.initial_speed;
	shoot_set_vision_data_container.vision_bias_time = vision_send_time;
	shoot_set_vision_data_container.vision_ID = ID_judge;
	shoot_set_vision_data_container.energy_flag = wlr.energy_flag;
	if(wlr.energy_flag && !g_robot_ctx.input.kb.bit.CTRL)//如果小能量机关正在激活、已激活
        shoot_set_vision_data_container.energy_state = 1;
    else if(wlr.energy_flag && g_robot_ctx.input.kb.bit.CTRL)//如果大能量机关正在激活、已激活
        shoot_set_vision_data_container.energy_state = 2;
    else
        shoot_set_vision_data_container.energy_state = 0;
	container_set(TAG_SHOOT_VISION_DATA,&shoot_set_vision_data_container,sizeof(shoot_set_vision_data_container),CONTAINER_TYPE_STRUCT);
}

static uint8_t single_shoot_reset(void)
{
    return (
        (rc.mouse.l == 0 && ctrl_mode == KEYBOARD_MODE)
        || (ABS(rc.ch3) < 10 && ctrl_mode == REMOTER_MODE)
    );
}

static uint8_t single_shoot_enable(void)
{
    return (
        shoot_enable
        && shoot.barrel.heat_remain >= MIN_HEAT
        && ((rc.mouse.l && ctrl_mode == KEYBOARD_MODE) || (rc.ch3 > 400 && ctrl_mode == REMOTER_MODE))
        && ABS(trigger_ecd_error) < 0.4f * TRIGGER_MOTOR_ECD_SINGLE
    );
}

static uint8_t series_shoot_enable(void)
{
	if(rc.kb.bit.G)
		shoot.trigger_period  = 60;
	else if (rc_fsm_check(RC_LEFT_LD) && rc_fsm_check(RC_RIGHT_LU) )
		shoot.trigger_period  = 83;
//	else
//		shoot.trigger_period = TRIGGER_PERIOD;
    return (
        ( (ctrl_mode == REMOTER_MODE && shoot_get_vision_data_container.vision_enanle) //&& ( rc_fsm_check(RC_LEFT_LD) && rc_fsm_check(RC_RIGHT_RD) ) ) //开启视觉连发
			|| (ctrl_mode == REMOTER_MODE && rc.sw2 == RC_DN && rc_fsm_check(RC_LEFT_LD) && !rc_fsm_check(RC_RIGHT_RD)  )  //开启遥控连发
			|| (ctrl_mode == KEYBOARD_MODE && rc.mouse.l && rc.mouse.r && rc.kb.bit.G)//直接射	
            || (ctrl_mode == KEYBOARD_MODE && rc.mouse.l && rc.mouse.r && shoot_get_vision_data_container.vision_enanle)
            || (ctrl_mode == KEYBOARD_MODE && rc.mouse.l && rc.mouse.r == 0)
        )    
				
        && ((shoot.barrel.heat_remain >= MIN_HEAT))  //热量控制
        && frequency_cnt * SHOOT_PERIOD >= shoot.trigger_period  //射频控制
        && ABS(trigger_ecd_error) <  0.05f * TRIGGER_MOTOR_ECD_SERIES  //拨盘误差控制		
    );
}
static uint16_t init_cnt = 0; 
static uint8_t recover_flag = 0;

//uint16_t sbtrigger = 37000;
//uint16_t sbtrigger = 52937;		//2026-5-11 0:29
//uint16_t sbtrigger = 31328;		//2026-5-13 0:38
uint16_t sbtrigger = 60822;			//2026-5-14 3:59
//uint16_t sbtrigger = 8000;

static void pre_fabricated_trigger_position(void)
{
	if(init_cnt < 1000)
	{	
		init_cnt++;
		if(recover_flag == 0)
		{ 
#ifdef MG4005
//			shoot.trigger_ecd.ref = 13418.0f/65535.0f;			//38400	50%的连发几率			//改这里改变预制的位置，通过读编码值
			shoot.trigger_ecd.ref = sbtrigger;
																//19970 一袋子的弹只连发4次

#endif
//
#ifdef DJI3508
//			trigger_motor.total_ecd = trigger_motor.ecd;
			shoot.trigger_ecd.ref = 4717.0f;	//5976
#endif
			recover_flag++; 
		}
	}
}

//记录电机的圈数
//void update_position(void)
//{
//	#ifdef MG4005
//		uint16_t current_encoder = trigger_motor_MG4005.raw_encoder;
//	    // 计算差值，考虑溢出
//		int16_t delta = (int16_t)(current_encoder - prev_encoder);  
//		// 如果差值过大，说明发生了溢出
//		// 例如从65530走到10，正常差值是10-65530 = -65520，这是一个巨大的负变化
//		if(delta > ENCODER_MAX / 2) {
//			delta -= ENCODER_MAX; // 正溢出，应减去一圈
//		} else if(delta < -ENCODER_MAX / 2) {
//			delta += ENCODER_MAX; // 负溢出，应加上一圈
//		}
//		// 更新电机总圈数 (delta / ENCODER_MAX 是电机转动的圈数)
//		total_motor_revolutions += (float)delta / ENCODER_MAX;
//		// 更新输出轴圈数
//		real_motor_revolutions = total_motor_revolutions / REDUCTION_RATIO;
//		prev_encoder = current_encoder;
//	#endif
//		
//	#ifdef DM2325
//		float current_encoder = trigger_motor_2325.position;
//		float delta = (float)(current_encoder - prev_encoder_2325);  
//		static uint8_t rec_cnt = 0;
//		static float p_offset = 0.0f;
////		static float total_p = 0.0f;
////		static float caccaa = 0.0f;
////		fuck = delta;
////		if(ABS(delta)>0.5)
////		{
//		if(rec_cnt < 50)
//		{
//			rec_cnt++;
//			p_offset = current_encoder;
//		}
//		else{
//				if(delta > ENCODER_MAX / 2) {
//						delta -= ENCODER_MAX; // 正溢出，应减去一圈
//				} else if(delta < -ENCODER_MAX / 2) {
//						delta += ENCODER_MAX; // 负溢出，应加上一圈
//				}
////				caccaa = delta / (ENCODER_MAX);
////    total_motor_revolutions += caccaa;
//			total_motor_revolutions	+= delta / (ENCODER_MAX);
//			real_motor_revolutions = total_motor_revolutions / 25.0f;
//		}
//		prev_encoder_2325 = current_encoder;
//	#endif
//}

uint8_t last_enable;
static void shoot_control(void)
{
    switch (shoot.trigger_mode) {
        case TRIGGER_MODE_PROTECT: { //拨盘保护模式，保持惯性，无力
            frequency_cnt = 0; //计时变量置0，打出当前一发，禁止
            shoot.barrel.shoot_period = 0;
            shoot.trigger_ecd.ref = trigger_motor.total_ecd;
            shoot.trigger_spd.pid.i_out = 0;
            shoot.trigger_output = 0;
			init_cnt = 0; 
			recover_flag = 0;
			trigger_motor.round_cnt = 0;
            break;
        }
        case TRIGGER_MODE_STOP: { //拨盘停止模式，保持静止，有力
            frequency_cnt = 0; //计时变量置0，打出当前一发，禁止
            shoot.barrel.shoot_period = 0;
            
            shoot.trigger_ecd.ref = trigger_motor.total_ecd;
            shoot.trigger_spd.pid.i_out = 0;
			
            break;
        }
        case TRIGGER_MODE_SINGLE: { //拨盘单发模式，连续开枪请求，只响应一次
            
			
//			pre_fabricated_trigger_position();

			frequency_cnt++;
            trigger_ecd_error = shoot.trigger_ecd.ref - shoot.trigger_ecd.fdb;
            if (single_shoot_reset()) {
                shoot_enable = 1;
            }
            if (single_shoot_enable()) { //热量控制
                shoot_enable = 0;
                shoot.trigger_ecd.ref += TRIGGER_MOTOR_ECD_SINGLE;
                shoot.barrel.heat += 10;
                shoot_delay_record_fire();
            }
            break;
        }
        case TRIGGER_MODE_SERIES: { //拨盘连发模式，连续开枪请求，连续响应
			
//			pre_fabricated_trigger_position();

			
            frequency_cnt++;
            trigger_ecd_error = shoot.trigger_ecd.ref - shoot.trigger_ecd.fdb;
           if (series_shoot_enable() && !back_flag) { //一个周期打一颗
                frequency_cnt = 0;
				shoot.trigger_ecd.ref += 1 * TRIGGER_MOTOR_ECD_SERIES;
			    shoot.trigger_ecd.pid.i_out = 0.0f;
                shoot.barrel.heat += 10;
                shoot_delay_record_fire();
            }
			//卡蛋反转
			if(ABS(trigger_motor.rx_current) > TRIGGER_MOTOR_STUCK_CURRENT && ABS(trigger_motor.speed_rpm) < TRIGGER_MOTOR_STUCK_SPEED ) {
				back_cnt ++;
			} else {
				back_cnt = 0;
				err_cnt = 0;
				back_flag = 0;
			}
			if (back_cnt > 50) {
				back_flag = 1;
				err_cnt ++;
				shoot.trigger_ecd.ref = trigger_motor.total_ecd - TRIGGER_MOTOR_ECD_SERIES;
				if (err_cnt > 200) {
					shoot.trigger_ecd.ref = trigger_motor.total_ecd;
					back_cnt = 0;
					err_cnt = 0;
					back_flag = 0;
					shoot.trigger_ecd.ref = trigger_motor.total_ecd;
				}
			}
            break;
        }
        default:break;
    }
    switch (shoot.fric_mode) {
        case FRIC_MODE_PROTECT:
        case FRIC_MODE_STOP: {
            shoot.fric_spd[0].ref = 0;
            shoot.fric_spd[1].ref = 0;
            break;
        }
        case FRIC_MODE_RUN: {
            shoot.fric_spd[0].ref = -shoot.fric_speed_set;
            shoot.fric_spd[1].ref = shoot.fric_speed_set;
            break;
        }
        default:break;
    }
		
	last_enable = vision.shoot_enable;
	global_back_flag = back_flag;
}

static void shoot_init(void)
{
    memset(&shoot, 0, sizeof(shoot_t));
    shoot_delay_reset();
    //发射器底层初始化
    pid_init(&shoot.fric_spd[0].pid, NONE, 0.0005f, 0, 0, 0, 0.8);
    pid_init(&shoot.fric_spd[1].pid, NONE, 0.0005f, 0, 0, 0, 0.8);
    #ifdef DJI2006
    pid_init(&shoot.trigger_ecd.pid, NONE, 0.12f, 0, 0.0f, 0, 10000);
    pid_init(&shoot.trigger_spd.pid, NONE, 0.0015f, 0.00005f, 0, 0.18f, 1.8f);
    #endif
    #ifdef MG4005
    pid_init(&shoot.trigger_ecd.pid, NONE, 0.15f, 0.0f, 0.15f, 0.0f, 30000.0f);
    pid_init(&shoot.trigger_spd.pid, NONE, 0.11f, 0.001f, 0.0f, 1500.0f, 2048.0f);
	
	
    #endif
    //发射器模式初始化
    shoot.trigger_mode  = TRIGGER_MODE_PROTECT;
    shoot.fric_mode     = FRIC_MODE_PROTECT;
    //枪管参数初始化
    shoot.trigger_period 		= TRIGGER_PERIOD;
    shoot.fric_speed_set 		= 780;
    shoot.barrel.cooling_rate   = 10;
    shoot.barrel.heat_max       = 50;
    //历史射速反馈缓存区
//    shoot_speed_buffer = buffer_create(SHOOT_SPEED_NUM, sizeof(float));
	container_bus_init(mb_callback, sizeof(mb_callback)/sizeof(ContainerBusCfg));
}

static void shoot_pid_calc(void)
{
    for (int i = 0; i < 2; i++) {
        shoot.fric_spd[i].fdb = fric_motor[i].velocity;
        shoot.fric_output[i] = pid_calc(&shoot.fric_spd[i].pid, shoot.fric_spd[i].ref, shoot.fric_spd[i].fdb);
    }
    shoot.trigger_ecd.fdb = trigger_motor.total_ecd;
    shoot.trigger_spd.ref = pid_calc(&shoot.trigger_ecd.pid, shoot.trigger_ecd.ref, shoot.trigger_ecd.fdb);
    shoot.trigger_spd.fdb = trigger_motor.speed_rpm;
    shoot.trigger_output = pid_calc(&shoot.trigger_spd.pid, shoot.trigger_spd.ref, shoot.trigger_spd.fdb);
		
	//牛牛卡了反转
	if(ABS(trigger_motor.rx_current) > TRIGGER_MOTOR_STUCK_CURRENT && ABS(trigger_motor.speed_rpm) < TRIGGER_MOTOR_STUCK_SPEED && back_flag == 1)
		shoot.trigger_output = 0;
}

static void shoot_data_output(void)
{
    if (shoot.fric_mode == FRIC_MODE_PROTECT) {
        dji_motor_set_torque(&fric_motor[0], 0);
        dji_motor_set_torque(&fric_motor[1], 0);
    } else {
        dji_motor_set_torque(&fric_motor[0], shoot.fric_output[0]);
        dji_motor_set_torque(&fric_motor[1], shoot.fric_output[1]);
    }
    if (shoot.trigger_mode == TRIGGER_MODE_PROTECT) {
        #ifdef DJI2006
        dji_motor_set_torque(&trigger_motor, 0);
        #endif
        #ifdef MG4005
        trigger_motor.tx_current = 0;
        #endif
    } else {
        #ifdef DJI2006
        dji_motor_set_torque(&trigger_motor, shoot.trigger_output);
        #endif
        #ifdef MG4005
        trigger_motor.tx_current = shoot.trigger_output;
        #endif
    }
}

static void shoot_param_update(void)
{
    //更新裁判系统数据
    if (robot_status.shooter_barrel_heat_limit != 0) {
        shoot.barrel.heat_max = robot_status.shooter_barrel_heat_limit;//枪管热量上限
        shoot.barrel.cooling_rate = robot_status.shooter_barrel_cooling_value;//枪管冷却速率
    }
    //更新模拟裁判系统数据
    shoot.barrel.heat -= shoot.barrel.cooling_rate * SHOOT_PERIOD * 0.001f;  //当前枪管(理论)热量
    if (shoot.barrel.heat < 0) shoot.barrel.heat = 0;
//    shoot.barrel.heat_remain = shoot.barrel.heat_max - shoot.barrel.heat;  //当前枪管(理论)剩余热量
	shoot.barrel.heat_remain = ((robot_status.shooter_barrel_heat_limit - power_heat_data.shooter_17mm_barrel_heat) / 3.0f * 1.0f + 
								(shoot.barrel.heat_max - shoot.barrel.heat) / 3.0f * 2.0f);//枪管(理论)剩余热量
//	if(fabsf(shoot.barrel.heat - power_heat_data.shoo2'ter_17mm_barrel_heat) > 100)
//		shoot.barrel.heat = power_heat_data.shooter_17mm_barrel_heat;
	if(shoot.trigger_mode != TRIGGER_MODE_SERIES || (ctrl_mode == KEYBOARD_MODE && !rc.mouse.l))
		shoot.barrel.heat = power_heat_data.shooter_17mm_barrel_heat;
//	shoot.barrel.heat_remain = 100;
}

static void shoot_mode_switch(void)
{
    /* 1. 更新裁判系统参数 (保持原样) */
    shoot_param_update();

    /* 2. 射频切换 (复刻你原版的右键高频逻辑) */
    if (ctrl_mode == KEYBOARD_MODE && rc.mouse.r)
	{
        shoot.trigger_period = (Heat_ShootPeriod_calc(shoot_get_vision_data_container.vision_trace_id) > 200.0f ? 200.0f : Heat_ShootPeriod_calc(shoot_get_vision_data_container.vision_trace_id));
	}
	else if(ctrl_mode == REMOTER_MODE && rc_fsm_check(RC_LEFT_LD) && rc_fsm_check(RC_RIGHT_RD))
		shoot.trigger_period = 100;
    else
        shoot.trigger_period = 100;

    /* 3. 解析 FSM 大脑的组合状态 */
    switch (g_robot_ctx.output.shoot) {
        case SHOOT_PROTECT:{
            shoot.fric_mode = FRIC_MODE_STOP;
            shoot.trigger_mode = TRIGGER_MODE_PROTECT;
			shoot_delay_reset();
            break;}
            
        case SHOOT_STOP:{
            shoot.fric_mode = FRIC_MODE_STOP;
            shoot.trigger_mode = TRIGGER_MODE_STOP;
            break;}
            
        case SHOOT_SINGLE:{
            shoot.fric_mode = FRIC_MODE_RUN;
            shoot.trigger_mode = TRIGGER_MODE_SINGLE;
            break;}
            
        case SHOOT_SERIES:{
            shoot.fric_mode = FRIC_MODE_RUN;
            shoot.trigger_mode = TRIGGER_MODE_SERIES;
            break;}
            
        default:{
            shoot.fric_mode = FRIC_MODE_STOP;
            shoot.trigger_mode = TRIGGER_MODE_PROTECT;
            break;}
    }

    /* 4. 视觉模式切换 (保留你原版的键位掩码判断) */
    if (ctrl_mode == KEYBOARD_MODE) {
        if (KEY_PRESS_VISION2) {
            vision.tx.data.aiming_mode = 2;
        } else if (KEY_PRESS_VISION1) {
            vision.tx.data.aiming_mode = 1;
        } else {
            vision.tx.data.aiming_mode = 0;
        }
    } else {
        vision.tx.data.aiming_mode = 0;
    }

    /* 5. 裁判系统掉电保护 (保留你原版的安全逻辑) */
    if (ctrl_mode == KEYBOARD_MODE) {
        if (!robot_status.power_management_shooter_output) {
            shoot.fric_mode = FRIC_MODE_PROTECT;
            shoot.trigger_mode = TRIGGER_MODE_STOP;
        }
    }
}

static void vision_shoot_delay(void){
    float cur_shoot_speed = shoot_data.initial_speed;
    uint32_t now_tick = osKernelSysTick();

    shoot_delay_drop_timeout(now_tick);

    if (ABS(cur_shoot_speed - last_shoot_speed) > SHOOT_SPEED_CHANGE_EPS) {
        if (shoot_delay_cnt > 0) {
            uint32_t delay_tick = now_tick - shoot_delay_queue[shoot_delay_head].start_tick;
            shoot_delay_time = (float)delay_tick;
            vision_send_time = median_filter((int)delay_tick);
            shoot_delay_update_cnt++;
            shoot_delay_pop();
        }
        last_shoot_speed = cur_shoot_speed;
    }
}

//根据当前热量线性控制射频
static float Heat_ShootPeriod_calc(uint8_t trice_id)
{
	//剩余热量为0->射频 = 0 
	//剩余热量为max->射频 = 40
	//最大射频再加入判断条件：随最大热量而变化
	//线性函数：
	if(trice_id == 1)
		return ((100.0f / shoot.barrel.heat_max) * shoot.barrel.heat_remain);
	else
		return ((125.0f / shoot.barrel.heat_max) * shoot.barrel.heat_remain);
}

void shoot_task(void const *argu)
{
    uint32_t thread_wake_time = osKernelSysTick();
    shoot_init();
    for(;;)
    {
        thread_wake_time = osKernelSysTick();
//        taskENTER_CRITICAL();
		vision_num();		//用于板间通信传输数据，但云台未使用
        shoot_mode_switch();    /* 发射器模式切换 */
        shoot_control();
        shoot_pid_calc();
        shoot_data_output();
		vision_shoot_delay();
		shoot_set_container();
        status.task.shoot = 1;
//        taskEXIT_CRITICAL();
        osDelayUntil(&thread_wake_time, 2);
    }
}
