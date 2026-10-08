#include "usart_comm.h"
#include "string.h"
#include "data_log.h"

/*
 * @brief  串口初始化，开启空闲中断并开始DMA接收数据
 * @retval void
 */
void usart_comm_init(void)
{
    log_init(&DEBUG_HUART);
}

/*
 * @brief  串口中断，添加各串口的数据接收函数
 * @retval void
 */
void usart_user_handler(UART_HandleTypeDef *huart)
{
    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_IDLE) != RESET) {
        __HAL_UART_CLEAR_IDLEFLAG(huart);
        HAL_UART_AbortReceive(huart);
    }
}
