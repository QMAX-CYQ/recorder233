#include "prot_power.h"
#include "mode_switch_task.h"
#include "wlr.h"
#include "chassis_task.h"
#include "can_comm.h"
#include "prot_judge.h"
#include "drv_dji_motor.h"
#include "string.h"
#include "robot_logic.h"

#define  TOQUE_COEFFICIENT      1.99688994e-6f
#define  K0 1.453e-07f
#define	 R0 1.23e-07f
#define  P0 3.081f

supercap_t supercap;
power_control_t power_control;

static const int target_indices[] = {0,1,2,3,10,11,12,13};//功率控制的K元素索引

void power_init(void)
{
    memset(&supercap, 0, sizeof(supercap_t));
    memset(&power_control, 0, sizeof(power_control_t));
    power_control.judge_power_buffer = 60.0f;
    power_control.judge_max_power    = 55;
    power_control.min_buffer         = 30;
    power_control.limit_kp           = 0.4f;
		power_control.rotate_add_power   = -10.0f;
		power_control.normal_add_power   = 120.0f;
//    power_control.limit_power        = 170.0f;//170 250
//    supercap.max_volage              = 23.6f;
	supercap.max_volage				 = 30.0f;
    supercap.min_volage              = 10.0f;
    supercap.volume_percent          = 100;
    supercap.volage                  = supercap.min_volage;
}

void power_judge_update(void)
{
	if(robot_status.chassis_power_limit != 0)
        power_control.judge_max_power     = robot_status.chassis_power_limit;
		
    power_control.judge_power_buffer  = power_heat_data.buffer_energy;
	if (g_robot_ctx.output.chassis == CHASSIS_LOW_SPIN){ 
		power_control.limit_power = power_control.judge_max_power + power_control.rotate_add_power;
    } else 
		power_control.limit_power = power_control.judge_max_power + power_control.normal_add_power;
}

float motor_power_calcu(float current, float wheel_speed_fdb)
{
    float power;
    power = TOQUE_COEFFICIENT * current * wheel_speed_fdb +
                (double)K0 * wheel_speed_fdb * wheel_speed_fdb +
                (double)R0 * current * current 
								+ P0;
    return power;
}

void power_limit_current(void)
{
    power_judge_update();
    power_control.total_power_wheel = 0;
    //未限功率前预测功率
    for (int i = 0; i < 2; i++) {
        power_control.give_power_wheel[i] = motor_power_calcu(driver_motor[i].tx_current, driver_motor[i].speed_rpm);
        if (power_control.give_power_wheel[i] < 0)
            continue;
        power_control.total_power_wheel += power_control.give_power_wheel[i];
    }
    if (power_control.total_power_wheel >= 350) {//功率超限重分配
        float a = 0, b = 0, c = 2 * P0 - 350;
        for (int i = 0; i < 2; i++) {
            a += R0 * driver_motor[i].tx_current * driver_motor[i].tx_current;
            b += TOQUE_COEFFICIENT * driver_motor[i].speed_rpm * driver_motor[i].tx_current;
            c -= K0 * driver_motor[i].speed_rpm * driver_motor[i].speed_rpm;
        }
        if (b * b - 4 * a * c >= 0) {//有解
            power_control.power_scale = (-b + sqrtf(b * b - 4 * a * c)) / (2 * a);
        } else {
            power_control.power_scale = -b / 2 / a;
        }
    } else {
        power_control.power_scale = 1.0f;
    }
    //限制电流
    driver_motor[0].tx_current = power_control.power_scale * driver_motor[0].tx_current;
    driver_motor[1].tx_current = power_control.power_scale * driver_motor[1].tx_current;
//		//限制lqr.K
//    for (int i = 0; i < 8; ++i) {
//        int idx = target_indices[i];
//        lqr.K[idx] *= power_control.power_scale;
//    }
    //限电流功率后预测功率
    power_control.total_power_wheel = 0;
    for (int i = 0; i < 2; i++) {
        power_control.give_power_wheel[i] =  motor_power_calcu(driver_motor[i].tx_current, driver_motor[i].speed_rpm);
        power_control.total_power_wheel += power_control.give_power_wheel[i];
    }
//    if (power_control.judge_power_buffer < power_control.min_buffer) {//限制速度 后面再写
//        
//    }
}

float power_limit_speed(void)
{
    power_judge_update();
    power_control.total_power_wheel = 0;
    //未限功率前预测功率
    for (int i = 0; i < 2; i++) {
        power_control.give_power_wheel[i] =  motor_power_calcu(driver_motor[i].tx_current, driver_motor[i].speed_rpm);
        if (power_control.give_power_wheel[i] < 0)
            continue;
        power_control.total_power_wheel += power_control.give_power_wheel[i];
    }
    //功率超限重分配 限速度方案
    float a = 2 * K0;
    float b = 0;
    float c = 2 * P0 - power_control.limit_power;
    for (int i = 0; i < 2; i++) {
        if (driver_motor[i].tx_current > 0) {
            b += 2 * TOQUE_COEFFICIENT * driver_motor[i].tx_current;
        } else {
            b -= 2 * TOQUE_COEFFICIENT * driver_motor[i].tx_current;
        }
        c += R0 * driver_motor[i].tx_current * driver_motor[i].tx_current;
    }
    if (b * b - 4 * a * c >= 0) {
        power_control.power_scale = (-b + sqrtf(b * b - 4 * a * c)) / (2 * a);
    } else {
        power_control.power_scale = -b / 2 / a;
    }
    return power_control.power_scale;
}

void power_get_data(uint8_t *data)
{
	
    //0x100
    float cap_voltage_buf;
    float chassis_current_buf;
    
    memcpy(&cap_voltage_buf, data, 4);
    memcpy(&chassis_current_buf, data + 4, 4);
    
    power_control.online = 1;
    supercap.volage = cap_voltage_buf;
    supercap.volume_percent = (supercap.volage - supercap.min_volage) / (supercap.max_volage - supercap.min_volage) * 100.0f;
		if(power_control.online == 0)
			 supercap.volume_percent = 0;
		
    supercap.current = chassis_current_buf;
		
	uint8_t cap_state_buf;
    memcpy(&cap_state_buf,data + 8,1);
    supercap.state.cap_v_over = cap_state_buf  & 1;
    supercap.state.cap_v_low = cap_state_buf >> 1 & 1;
    supercap.state.bat_v_over = cap_state_buf >> 2 & 1;
    supercap.state.bat_v_low = cap_state_buf >> 3 & 1;
    supercap.state.cap_i_over = cap_state_buf >> 4 & 1;
    supercap.state.chassis_i_over = cap_state_buf >> 5 & 1;
    supercap.state.chassis_msg_miss = cap_state_buf >> 6 & 1;
    supercap.state.judge_msg_miss = cap_state_buf >> 7;
	memcpy(&supercap.power_mode,data + 9,1);
    //0x101
//    uint8_t cap_state_buf;
//    memcpy(&cap_state_buf,data,1);
//    supercap.state.cap_v_over = cap_state_buf  & 1;
//    supercap.state.cap_v_low = cap_state_buf >> 1 & 1;
//    supercap.state.bat_v_over = cap_state_buf >> 2 & 1;
//    supercap.state.bat_v_low = cap_state_buf >> 3 & 1;
//    supercap.state.cap_i_over = cap_state_buf >> 4 & 1;
//    supercap.state.chassis_i_over = cap_state_buf >> 5 & 1;
//    supercap.state.chassis_msg_miss = cap_state_buf >> 6 & 1;
//    supercap.state.judge_msg_miss = cap_state_buf >> 7;
//    memcpy(&supercap.POWER_MODE,data+1,sizeof(power_mode));

}

void power_get_status(uint8_t *data)
{
    //0x101
    uint8_t cap_state_buf;
    memcpy(&cap_state_buf,data,1);
    supercap.state.cap_v_over = cap_state_buf  & 1;
    supercap.state.cap_v_low = cap_state_buf >> 1 & 1;
    supercap.state.bat_v_over = cap_state_buf >> 2 & 1;
    supercap.state.bat_v_low = cap_state_buf >> 3 & 1;
    supercap.state.cap_i_over = cap_state_buf >> 4 & 1;
    supercap.state.chassis_i_over = cap_state_buf >> 5 & 1;
    supercap.state.chassis_msg_miss = cap_state_buf >> 6 & 1;
    supercap.state.judge_msg_miss = cap_state_buf >> 7;
//    memcpy(&supercap.POWER_MODE,data+1,sizeof(power_mode));
}	





static float power_velocity_table[11][2] = {
    {35.0f ,1.1f},//节能状态
    {45.0f ,1.3f},//1
    {50.0f ,1.3f},//2
    {55.0f ,1.4f},//3
    {60.0f ,1.4f},//4
    {65.0f ,1.5f},//5
    {70.0f ,1.6f},//6
    {75.0f ,1.6f},//7
    {80.0f ,1.7f},//8
    {90.0f ,1.8f},//9
    {100.0f,1.9f},//10
};

static float power_rotate_table[11][2] = {
    {35.0f ,5.0f},//节能状态
    {45.0f ,7.0f},//1
    {50.0f ,7.0f},//2
    {55.0f ,8.0f},//3
    {60.0f ,8.0f},//4
    {65.0f ,9.0f},//5
    {70.0f ,9.0f},//6
    {75.0f ,10.0f},//7
    {80.0f ,10.0f},//8
    {90.0f ,11.0f},//9
    {100.0f,12.0f},//10
};
static float supercap_velocity_addmap(void)
{
    if(supercap.volume_percent < 50.0f)
        return 0.0f;
    else
        return (supercap.volume_percent - 50.0f) / 50.0f * 0.4f;//线性映射，电容电压从40%到100%时，速度增加0-0.4f
}

float power_control_target_velocity(void)
{
    power_judge_update();
	static float base_velocity = 0.0f;
    base_velocity = power_velocity_table[0][1];
    //更新基础速度
    for(uint8_t i = 0; i < 11; i++) {
        if (power_control.judge_max_power == power_velocity_table[i][0]){
            base_velocity = power_velocity_table[i][1];
            break;
		}
    }
    //根据电容电压进行微调
    return (base_velocity + supercap_velocity_addmap());
}

float power_control_target_Vrotate(void)
{
    power_judge_update();
	static float base_rotate = 0.0f;
    base_rotate = power_rotate_table[0][1];
    //更新基础速度
    for(uint8_t i = 0; i < 11; i++) {
        if (power_control.judge_max_power == power_rotate_table[i][0]){
            base_rotate = power_rotate_table[i][1];
            break;
		}
    }
    //根据电容电压进行微调
    return (base_rotate + supercap_velocity_addmap());
}

uint8_t power_check_offline(void)
{
    if (power_control.online == 0) {
        return 1;
    } else {
        power_control.online = 0;
        return 0;
    }
}

