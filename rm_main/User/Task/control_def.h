#ifndef __CONTROL_DEF_H
#define __CONTROL_DEF_H

#include "stdint.h"

/*----------------------------- player preference ----------------------------- */
#define KEY_CHASSIS_FIGHT       KB_F
#define KEY_CHASSIS_FIGHT_A     KB_A
#define KEY_CHASSIS_FIGHT_D     KB_D
#define KEY_CHASSIS_ROTATE      KB_R

#define KEY_VISION     			KB_X

#define KEY_CHASSIS_POWER       KB_SHIFT

#define KEY_GIMBAL_TURN_R       KB_E
#define KEY_GIMBAL_TURN_L       KB_Q

#define KEY_PRESS_VISION1       rc.kb.bit.B //大符
#define KEY_PRESS_VISION2       rc.kb.bit.G //小符
#define KEY_PRESS_POWER         rc.kb.bit.SHIFT//高速

/*-----------------------------shoot-----------------------------*/
//拨盘频率
#define TRIGGER_PERIOD      250//ms 90->11Hz 40->25Hz 33->30Hz
//#define TRIGGER_PERIOD2     40//40 55->18Hz     
#define TRIGGER_PERIOD2     250//40 55->18Hz 

/*-----------------------------chassis---------------------------*/

#define SUPERCAP_CHAGER_VOLAGE    23.6f
#define SUPERCAP_DISCHAGER_VOLAGE	13.5f //超级电容放电电压下限

#define CHASSIS_YAW_OFFSET  1565		//7427
#define CHASSIS_YAW_FIGHT   ((CHASSIS_YAW_OFFSET + 8192/4) % 8192)
#define CHASSIS_ROTATE_SPEED  10.0f //rad/s --- 7.5

/*-----------------------------gimbal----------------------------*/

#define GIMBAL_PIT_CENTER_OFFSET    5400
#define GIMBAL_PIT_MAX              6400
#define GIMBAL_PIT_MIN              5000

#endif
