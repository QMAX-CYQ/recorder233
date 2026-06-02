#include "prot_dr16.h"
#include "mode_switch_task.h"
#include "chassis_task.h"
#include "can_comm.h"
#include "prot_imu.h"
#include "string.h"
#include "prot_judge.h"
#include "prot_vision.h"
#include "chassis_task.h"
#include "shoot_task.h"
#include "gimbal_task.h"

#define ABS(x)  ((x)>0?(x):(-(x)))

//#define RC_LEFT_LU_CH_VALUE  ( rc.ch3 < -500 && rc.ch4 >  500 )   //居左上
//#define RC_LEFT_RU_CH_VALUE  ( rc.ch3 >  500 && rc.ch4 >  500 )   //居右上
//#define RC_LEFT_RD_CH_VALUE  ( rc.ch3 >  500 && rc.ch4 < -500 )   //居右下
//#define RC_LEFT_LD_CH_VALUE  ( rc.ch3 < -500 && rc.ch4 < -500 )   //居左下
//#define RC_RIGHT_LU_CH_VALUE ( rc.ch2 >  500 && rc.ch1 < -500 )   //居左上
//#define RC_RIGHT_RU_CH_VALUE ( rc.ch2 >  500 && rc.ch1 >  500 )   //居右上
//#define RC_RIGHT_RD_CH_VALUE ( rc.ch2 < -500 && rc.ch1 >  500 )   //居右下
//#define RC_RIGHT_LD_CH_VALUE ( rc.ch2 < -500 && rc.ch1 < -500 )   //居左下

dr16_t rc;
dr16_t vtm;
int kb_status[15] = {0};

/*
 * @brief     dr16遥控器数据接收函数
 * @param[in] rc: 遥控器数据结构体
 * @param[in] dat	a: 数据指针
 * @retval    数据正常返回0，异常返回1
 */
uint8_t dr16_get_data(dr16_t *rc, uint8_t *data)
{
    rc->ch1 = (data[0]      | data[1]  << 8) & 0x07FF;
    rc->ch1 -= 1024;
    rc->ch2 = (data[1] >> 3 | data[2]  << 5) & 0x07FF;
    rc->ch2 -= 1024;
    rc->ch3 = (data[2] >> 6 | data[3]  << 2 | data[4] << 10) & 0x07FF;
    rc->ch3 -= 1024;
    rc->ch4 = (data[4] >> 1 | data[5]  << 7) & 0x07FF;
    rc->ch4 -= 1024;
    rc->ch5 = (data[16]     | data[17] << 8) & 0x07FF;
    rc->ch5 -= 1024;
    rc->sw1 = ((data[5] >> 4) & 0x000C) >> 2;
    rc->sw2 = (data[5] >> 4) & 0x0003;
    if ((ABS(rc->ch1) > 660) || (ABS(rc->ch2) > 660) || \
        (ABS(rc->ch3) > 660) || (ABS(rc->ch4) > 660) ||
        (rc->sw1 == 0) || (rc->sw2 == 0)) {
        memset(rc, 0, sizeof(dr16_t));
        return 1;
    }
    rc->mouse.x = data[6] | (data[7] << 8);
    rc->mouse.y = data[8] | (data[9] << 8);
    rc->mouse.z = data[10] | (data[11] << 8);
    rc->mouse.l = data[12];
    rc->mouse.r = data[13];
    rc->kb.key_code = data[14] | data[15] << 8;
    
    rc->online = 1;
    return 0;
}

void dr16_output_data(void)
{
    rc.tx1.data.ch1 = rc.ch1;
    rc.tx1.data.ch2 = rc.ch2;
    rc.tx1.data.sw1 = rc.sw1;
    rc.tx1.data.sw2 = rc.sw2;

    can_std_transmit(CAN_CHANNEL_3, 0x001, rc.tx1.buff);
}

void kb_output_data(void)
{
    rc.tx2.data.x = rc.mouse.x;
    rc.tx2.data.y = rc.mouse.y;
    rc.tx2.data.l = rc.mouse.l;
    rc.tx2.data.r = rc.mouse.r;
    rc.tx2.data.kb.key_code = rc.kb.key_code;
    
    can_std_transmit(CAN_CHANNEL_3, 0x001, rc.tx2.buff);
}
    
void imu_output_data(void)
{
    rc.tx3.data.chassis_pit 	= chassis_imu.pit;
    rc.tx3.data.ctrl_mode 		= ctrl_mode;
	
	if (chassis.recover_flag == 1 && fabs(chassis_imu.pit) > 0.3f)
		rc.tx3.data.ctrl_mode = PROTECT_MODE;
	
    rc.tx3.data.camp 			= robot_status.robot_id; 
    rc.tx3.data.rc_init_status  = rc.init_status;
    can_std_transmit(CAN_CHANNEL_3, 0x002, rc.tx3.buff);
}
  
void judge_output_data(void)
{
    rc.tx4.data.vision_ID = ID_judge;
    can_std_transmit(CAN_CHANNEL_3, 0x005, rc.tx4.buff);
}

void shoot_output_data(void)
{
	rc.tx5.data.vision_bias_time = vision_send_time;
	rc.tx5.data.shoot_speed 	 = shoot_data.initial_speed;
    can_std_transmit(CAN_CHANNEL_3, 0x007, rc.tx5.buff);
}

void gimbal_stable_output_data(void)
{
	rc.tx6.data.feedback_alpha_speed_output = gimbal_stable.feedback_alpha_speed;
	rc.tx6.data.feedback_beta_speed_output 	= gimbal_stable.feedback_beta_speed;
    can_std_transmit(CAN_CHANNEL_3, 0x008, rc.tx6.buff);
}

static uint16_t key_map(key_index_e key_index)
{
    uint16_t key;
    switch (key_index) {
        case KB_Q:     key = rc.kb.bit.Q;      break;
        case KB_E:     key = rc.kb.bit.E;      break;
        case KB_R:     key = rc.kb.bit.R;      break;
        case KB_F:     key = rc.kb.bit.F;      break;
        case KB_G:     key = rc.kb.bit.G;      break;
        case KB_Z:     key = rc.kb.bit.Z;      break;
        case KB_X:     key = rc.kb.bit.X;      break;
        case KB_C:     key = rc.kb.bit.C;      break;
        case KB_V:     key = rc.kb.bit.V;      break;
        case KB_B:     key = rc.kb.bit.B;      break;
        case KB_CTRL:  key = rc.kb.bit.CTRL;   break;
        case KB_SHIFT: key = rc.kb.bit.SHIFT;  break;
		case KB_A:	   key = rc.kb.bit.A;      break;
        case KB_D: 	   key = rc.kb.bit.D;      break;			
        case KB_NULL:  key = 0;                break; //无效按键
        default: break;
    }
    return key;
}
/**
 * @brief     按键扫描，按一次对应状态取反
 * @param[in] key_index: 按键序号
 * @retval    void
 */
uint8_t key_scan(key_index_e key_index)
{
    static uint8_t key_press_enable[15] = {0};
    uint16_t key = key_map(key_index);  			//获取当前按键状态
    if (key && key_press_enable[key_index] == 0) {  //如果当前按键按下且之前没有按下
        key_press_enable[key_index] = 1;  			//记录上一次状态，防止重复进入
        kb_status[key_index] = !kb_status[key_index];  //全局状态标志位取反 0-1
    } else if (key == 0) {
        key_press_enable[key_index] = 0;  			//允许下一次进入
    }
    return kb_status[key_index];
}

void key_status_clear(key_index_e key_index)
{
    kb_status[key_index] = KEY_END;
}

uint8_t key_scan_clear(key_index_e key_index)
{
    uint8_t res = 0;
    key_scan(key_index);	//如果按下一次按键，则反转标志位
    if (kb_status[key_index] == KEY_RUN) {
        kb_status[key_index] = KEY_END;  //清除标志位
        res = 1;			//返回TRUE
    } else if (kb_status[key_index] == KEY_END) {
        res = 0;
    }
    return res;
}

void rc_fsm_init(uint8_t trig_flag)
{
    static uint8_t last_trig_flag;
    static uint8_t state;
    if (trig_flag == 1 && last_trig_flag == 0) {  //检测到触发信号上升沿
        if (RC_LEFT_LU_CH_VALUE)   state |= RC_LEFT_LU;
        if (RC_LEFT_RU_CH_VALUE)   state |= RC_LEFT_RU;
        if (RC_LEFT_RD_CH_VALUE)   state |= RC_LEFT_RD;
        if (RC_LEFT_LD_CH_VALUE)   state |= RC_LEFT_LD;
        if (RC_RIGHT_LU_CH_VALUE)  state |= RC_RIGHT_LU;
        if (RC_RIGHT_RU_CH_VALUE)  state |= RC_RIGHT_RU;
        if (RC_RIGHT_RD_CH_VALUE)  state |= RC_RIGHT_RD;
        if (RC_RIGHT_LD_CH_VALUE)  state |= RC_RIGHT_LD;
    } else if (trig_flag == 0 && last_trig_flag == 1){  //遥控器断开连接
        state = 0x00;
    }
    last_trig_flag = trig_flag;
    rc.init_status = state;
    if (!trig_flag && last_trig_flag) {  //遥控器失联，状态器复位
        rc.init_status = 0;
    }
}

uint8_t rc_fsm_check(uint8_t target_status)
{
    uint8_t res = 0;
    if (rc.init_status & target_status) {
        res = 1;
    }
    return res;
}

uint8_t rc_check_offline(void)
{
    if (rc.online == 0) {
        return 1;
    } else {
        rc.online = 0;
        return 0;
    }
}
