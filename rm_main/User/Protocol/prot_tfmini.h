
#ifndef __TF_MINI_PLUS_H
#define __TF_MINI_PLUS_H

#include "stm32h7xx_hal.h"
#include "usart.h"


typedef enum {
    LEFT, 
	RIGHT,
}TF_Distance_e;

/*TFminiplus 处理后的数据*/
extern uint16_t tfmin_distance[2];
extern uint16_t tfmin_distance_average[2][100];

void vTfGetData(uint8_t *buff,TF_Distance_e TF_Distance);
#endif

