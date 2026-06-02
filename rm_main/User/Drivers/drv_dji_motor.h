#ifndef __DRV_DJI_MOTOR_H
#define __DRV_DJI_MOTOR_H

#include "can_comm.h"
#include "data_list.h"

/*-----------------------------------------
电机控制数据
型号以及对应电调    发送控制数据范围    对应物理量   转矩常数    力矩范围
2006(C610)          -10000~10000        +-10A       0.18N*m/A   +-1.8N*m
3508(C620)          -16384~16384        +-20A       0.3N*m/A    +-6N*m
6020(电流)          -16384~16384        +-3A        0.741N*m/A  +-2.223N*m
-----------------------------------------*/

//电机类型定义
#define DJI_2006_MOTOR 0x00
#define DJI_3508_MOTOR 0x01
#define DJI_6020_MOTOR 0x02

//发送数据范围
#define DJI_2006_MOTOR_DATA_RANGE 10000
#define DJI_3508_MOTOR_DATA_RANGE 16384
#define DJI_6020_MOTOR_DATA_RANGE 25000

//发送数据所代表的物理量范围
#define DJI_2006_MOTOR_CURRENT_RANGE 10
#define DJI_3508_MOTOR_CURRENT_RANGE 20
#define DJI_6020_MOTOR_CURRENT_RANGE 3

//力矩常数 0.01578947368421052631578947368421
#define DJI_2006_MOTOR_TORQUE_CONSTANT 0.18f
//#define DJI_3508_MOTOR_TORQUE_CONSTANT 0.246f//0.3f
#define DJI_3508_MOTOR_TORQUE_CONSTANT 0.2565625f//0.3f
//#define DJI_3508_MOTOR_TORQUE_CONSTANT 0.3f//0.3f
#define DJI_6020_MOTOR_TORQUE_CONSTANT 0.741f

//电机默认减速比
#define DJI_2006_ORIGINAL_REDUCTION_RATIO 36.0f
#define DJI_3508_ORIGINAL_REDUCTION_RATIO 19.2f
#define DJI_6020_ORIGINAL_REDUCTION_RATIO 1.0f

//自研减速箱减速比
#define DJI_3508_WHEEL_TAURUS_REDUCTION_RATIO   	16.42f  
#define DJI_3508_TRIGGER_TAURUS_REDUCTION_RATIO		5.0f

//ID 膝关节超前定义左右
#define DRIVER_MOTOR_LEFT_ID  0x204
#define DRIVER_MOTOR_RIGHT_ID 0x203

#define YAW_MOTOR_ID		  0x205
//#define TRIGGER_MOTOR_ID	  0x202

typedef struct
{
    uint32_t id;
    uint8_t data[8];
} can_std_msg_t;

typedef enum
{
	DJI_MOTOR_OK                  = 0,   // 无异常
    DJI_MOTOR_STORAGE_ACCESS_FAIL = 1,   // 无法访问电机中的存储芯片（仅开机自检）
    DJI_MOTOR_VOLTAGE_TOO_HIGH    = 2,   // 电调供电电压过高（仅开机自检）
    DJI_MOTOR_PHASE_NOT_CONNECTED = 3,   // 电机三相线未接入
    DJI_MOTOR_SENSOR_DATA_LOSS    = 4,   // 与电机相连的数据线中位置传感器数据丢失
    DJI_MOTOR_TEMP_ABNORMAL       = 5,   // 电机温度异常或过高 (≥180°C)
    DJI_MOTOR_CALIBRATION_FAILED  = 7,   // 电机校准失败
    DJI_MOTOR_OVERHEAT            = 8    // 电机过热 (≥125°C)
} dji_state_e;

typedef struct
{
    list_t list;
    //电机参数
    uint8_t motor_type;
    can_channel_e can_channel;
    uint32_t can_id;
    float reduction_ratio;              //当前减速比
    uint32_t send_cnt, receive_cnt;
    float err_percent;
    uint8_t online;
    //控制数据
    float t;
    //反馈数据
    float position, velocity, torque;   //rad rad/s rad/s/s N*m
    //中间数据
    uint16_t ecd;                       //当前编码值         单位：ecd 0~8191
    int16_t  speed_rpm;                 //转速               单位：rpm
    int16_t  rx_current, tx_current;    //实际转矩电流值     单位：
    uint8_t	 temperature;               //电机温度           单位：摄氏度
	dji_state_e  state;					//电机错误码（新版C620添加）
    uint16_t offset_ecd;                //刚上电时的编码值
    uint16_t last_ecd;                  //上次接收的编码值
    int32_t  total_ecd;                 //上电到现在转动的总编码值
    int32_t	 round_cnt;                 //已转圈数
} dji_motor_t;

extern dji_motor_t fric_motor[2];
extern dji_motor_t driver_motor[2];
extern dji_motor_t pit_motor, yaw_motor;
//extern dji_motor_t trigger_motor;

void dji_motor_init(dji_motor_t *motor, uint8_t motor_type, can_channel_e can_periph, uint32_t id, float reduction_ratio);
void dji_motor_get_data(can_channel_e can_periph, uint32_t id, uint8_t *data);
void dji_motor_set_torque(dji_motor_t *motor, float t);
void dji_motor_output_data(void);

uint8_t dji_motor_check_offline(void);

#endif
