#ifndef __USART_COMM_H
#define __USART_COMM_H

#include "stm32h7xx.h"
#include "usart.h"

//串口定义
#define	DBUS_HUART	 huart1
#define JUDGE_HUART  huart2
#define MS53L2M_RIGHT_HUART huart4
//#define TF_RIGHT_HUART huart4
#define MS53L2M_LEFT_HUART huart10

#define DEBUG_HUART	 huart6

void usart_comm_init(void);
void usart_user_handler(UART_HandleTypeDef *huart);

#endif
