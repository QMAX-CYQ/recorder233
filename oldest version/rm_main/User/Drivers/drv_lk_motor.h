#ifndef __DRV_LK_MOTOR_H
#define __DRV_LK_MOTOR_H

#include "can_comm.h"
#include "data_list.h"


#define LK_MOTOR_ECD_TO_RAD            0.00549324788281f //     360 /65535   (如果是MG4005 就要360/655350 多一个0是电机减速比1：10）


//ID
#define TRIGGER_MOTOR_ID 0x142

typedef enum
{
	CURRENT_MODE = 0xA1,
	READ_ERR	 = 0x9A,
	CLEAR_ERR	 = 0x9B,
	START_MOTOR  = 0x88,
	CLOSE_MOTOR	 = 0x80,
}lk_motor_cmd_e;
typedef struct
{
	list_t list;
	//电机参数
	uint8_t motor_type;
	can_channel_e can_channel;	
	uint32_t can_id;
	uint32_t send_cnt, receive_cnt;
	float err_percent;
	//零点
	float zero_point;	
	//发送力矩电流
	int16_t tx_current;
	//反馈初始数据
	int8_t temp;				//温度
	int16_t rx_current;			//反馈电流
	int16_t speed_rpm;				// °/s
	uint16_t ecd,last_ecd,offset_ecd;		//反馈编码器值
	//处理后的位置速度力矩数据
	int32_t round_cnt;			//电机转过的圈数
	int32_t total_ecd;			//总编码值
	float p, v, t;		
	uint8_t err_code;
	uint8_t online;
}lk_motor_t;


extern lk_motor_t trigger_motor;
void lk_motor_init(lk_motor_t *motor, can_channel_e can_channel, uint32_t id, float zero_point);
void lk_set_cmd(lk_motor_t* motor,lk_motor_cmd_e cmd);
void lk_set_current(lk_motor_t* motor);
void lk_get_single_data(lk_motor_t *motor,uint8_t *data);

#endif
