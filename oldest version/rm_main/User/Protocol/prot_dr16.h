#ifndef __PROT_DR16_H
#define __PROT_DR16_H

#include "stm32h7xx.h"

#define DR16_DATA_LEN 18

#define RC_LEFT_LU  ( 1<<0 ) //居左上 遥控器切换DEBUG灯板
#define RC_LEFT_RU  ( 1<<1 ) //居右上 遥控器注释底盘
#define RC_LEFT_RD  ( 1<<2 ) //居右下 遥控器注释发射
#define RC_LEFT_LD  ( 1<<3 ) //居左下 遥控器开启视觉		//外八开视觉
#define RC_RIGHT_LU ( 1<<4 ) //居左上 遥控器切换DEBUG灯板
#define RC_RIGHT_RU ( 1<<5 ) //居右上 遥控器注释底盘
#define RC_RIGHT_RD ( 1<<6 ) //居右下 遥控器注释发射
#define RC_RIGHT_LD ( 1<<7 ) //居左下 遥控器开启视觉

#define RC_LEFT_LU_CH_VALUE  ( rc.ch3 < -500 && rc.ch4 >  500 )   //居左上
#define RC_LEFT_RU_CH_VALUE  ( rc.ch3 >  500 && rc.ch4 >  500 )   //居右上
#define RC_LEFT_RD_CH_VALUE  ( rc.ch3 >  500 && rc.ch4 < -500 )   //居右下
#define RC_LEFT_LD_CH_VALUE  ( rc.ch3 < -500 && rc.ch4 < -500 )   //居左下
#define RC_RIGHT_LU_CH_VALUE ( rc.ch2 >  500 && rc.ch1 < -500 )   //居左上
#define RC_RIGHT_RU_CH_VALUE ( rc.ch2 >  500 && rc.ch1 >  500 )   //居右上
#define RC_RIGHT_RD_CH_VALUE ( rc.ch2 < -500 && rc.ch1 >  500 )   //居右下
#define RC_RIGHT_LD_CH_VALUE ( rc.ch2 < -500 && rc.ch1 < -500 )   //居左下

typedef __PACKED_STRUCT
{
	int16_t ch1;
	int16_t ch2;
	int16_t ch3;
	int16_t ch4;
	int16_t ch5;
	uint8_t sw1;
	uint8_t sw2;
	__PACKED_STRUCT
	{
		int16_t x;
		int16_t y;
		int16_t z;
		uint8_t l;
		uint8_t r;
	} mouse;
	__packed union
	{
		uint16_t key_code;
		__PACKED_STRUCT
		{
			uint16_t W:1;
			uint16_t S:1;
			uint16_t A:1;
			uint16_t D:1;
			uint16_t SHIFT:1;
			uint16_t CTRL:1;
			uint16_t Q:1;
			uint16_t E:1;
			uint16_t R:1;
			uint16_t F:1;
			uint16_t G:1;
			uint16_t Z:1;
			uint16_t X:1;
			uint16_t C:1;
			uint16_t V:1;
			uint16_t B:1;//16个键位
		} bit;
	} kb;
    uint8_t init_status;//遥控器上电拨杆状态机
    uint8_t online;
    
    //发送rc数据给云台
    __packed union
    {
        uint8_t buff[8];
        __packed struct
        {
            int16_t ch1;
            int16_t ch2;
            uint8_t sw1;
            uint8_t sw2; 
            uint8_t empty1;
            uint8_t empty2;
        } data;        
    } tx1;
    
    __packed union
    {
        uint8_t buff[8];
        __packed struct
        {
            int16_t x;
            int16_t y;
            uint8_t l;
            uint8_t r;
            __packed union
            {
                uint16_t key_code;
                __PACKED_STRUCT
                {
                    uint16_t W:1;
                    uint16_t S:1;
                    uint16_t A:1;
                    uint16_t D:1;
                    uint16_t SHIFT:1;
                    uint16_t CTRL:1;
                    uint16_t Q:1;
                    uint16_t E:1;
                    uint16_t R:1;
                    uint16_t F:1;
                    uint16_t G:1;
                    uint16_t Z:1;
                    uint16_t X:1;
                    uint16_t C:1;
                    uint16_t V:1;
                    uint16_t B:1;//16个键位
                } bit;
            } kb;
        } data;
    } tx2;
    
    __packed union
    {        
        uint8_t buff[8];
        __packed struct
        {
            float chassis_pit;
            uint8_t ctrl_mode;
            uint8_t camp;
            uint8_t rc_init_status;
            uint8_t empty2;
        } data;
    } tx3;
		
    __packed union
    {        
        uint8_t buff[8];
        __packed struct
        {
            uint8_t vision_ID;
            uint8_t empty1;
			uint8_t empty2;
			uint8_t empty3;
			uint8_t empty4;
		    uint8_t empty5;
			uint8_t empty6;
			uint8_t empty7;
        } data;
    } tx4;
	
	 __packed union
    {        
        uint8_t buff[8];
        __packed struct
        {
			float vision_bias_time;
			float shoot_speed;
        } data;
		
    } tx5;
	
	 __packed union
    {        
        uint8_t buff[8];
        __packed struct
        {
			float feedback_alpha_speed_output;
			float feedback_beta_speed_output;
        } data;
		
    } tx6;	
		
} dr16_t;

typedef enum
{
    KB_Q = 0,
    KB_E = 1,
    KB_R = 2,
    KB_F = 3,
    KB_G = 4,
    KB_Z = 5,
    KB_X = 6,
    KB_C = 7,
    KB_V = 8,
    KB_B = 9,
    KB_CTRL = 10,
    KB_SHIFT = 11,
	KB_A = 12,
    KB_D = 13,
    KB_NULL = 14  //用户不想使用的按键集
} key_index_e;

typedef enum
{
    RC_UP = 1,
    RC_MI = 3,
    RC_DN = 2,
} rc_sw_mode_e;

typedef enum
{
    KEY_RUN = 1,
    KEY_END = 0
} rc_key_status_e;

extern dr16_t rc;
extern dr16_t vtm;
extern int kb_status[15];

uint8_t dr16_get_data(dr16_t *rc, uint8_t *data);
uint8_t key_scan(key_index_e key_index);
void key_status_clear(key_index_e key_index);
uint8_t key_scan_clear(key_index_e key_index);
void rc_fsm_init(uint8_t trig_flag);
uint8_t rc_fsm_check(uint8_t target_status);
void dr16_output_data(void);
void imu_output_data(void);
void kb_output_data(void);
void judge_output_data(void);
void shoot_output_data(void);
void gimbal_stable_output_data(void);
uint8_t rc_check_offline(void);

#endif
