#ifndef __USART_COMM_H
#define __USART_COMM_H

#include "stm32f4xx.h"                  // Device header

#include "usart.h"

//串口定义
#define DEBUG_HUART	 huart6

void usart_comm_init(void);
void usart_user_handler(UART_HandleTypeDef *huart);

#endif
