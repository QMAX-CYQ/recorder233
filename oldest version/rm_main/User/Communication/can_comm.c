#include "can_comm.h"
#include "drv_dji_motor.h"
#include "drv_ht_motor.h"
#include "drv_lk_motor.h"
#include "prot_imu.h"
#include "prot_power.h"
#include "fdcan.h"
#include "drv_dm_motor.h"
#include "prot_vision.h"
#include "board_comm.h"

FDCAN_TxHeaderTypeDef can_tx_message;
FDCAN_TxHeaderTypeDef fdcan_tx_message;
FDCAN_RxHeaderTypeDef rx_fifo0_message, rx_fifo1_message;
FDCAN_RxHeaderTypeDef fdcan_rx_fifo0_message, fdcan_rx_fifo1_message;
//注意FDCAN只能设置64个字节给用，设置8个会数组越界进硬件错误中断
uint8_t rx_fifo0_data[64], rx_fifo1_data[64];
uint8_t fdcan_rx_fifo0_data[64], fdcan_rx_fifo1_data[64];

uint32_t r_cnt;

uint8_t yaw_raw_data[8];

/*
 * @brief  can总线初始化
 * @retval void
 * @note   设置过滤器，添加各驱动的初始化函数
 */
void can_comm_init(void)
{
    FDCAN_FilterTypeDef can_filter;    
	//can1 filter config
	//step motor -- dm8009p * 4
	can_filter.IdType = FDCAN_STANDARD_ID;//STANDARD
    can_filter.FilterIndex = 0;
    can_filter.FilterType = FDCAN_FILTER_DUAL;//DUAL
    can_filter.FilterID1 = JOINT_RS_REC_ID;
    can_filter.FilterID2 = JOINT_RB_REC_ID;
    can_filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;//To Fifo0
	HAL_FDCAN_ConfigFilter(&hfdcan1, &can_filter);
	
	can_filter.IdType = FDCAN_STANDARD_ID;//STANDARD
    can_filter.FilterIndex = 1;
    can_filter.FilterType = FDCAN_FILTER_DUAL;//DUAL
    can_filter.FilterID1 = JOINT_LS_REC_ID;
    can_filter.FilterID2 = JOINT_LB_REC_ID;
    can_filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO1;//To Fifo1
    HAL_FDCAN_ConfigFilter(&hfdcan1, &can_filter);
    
	HAL_FDCAN_ConfigGlobalFilter(&hfdcan1, FDCAN_REJECT, FDCAN_REJECT, FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE);
    HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0);//使能邮箱0新消息中断
    HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO1_NEW_MESSAGE, 0);//使能邮箱1新消息中断
    HAL_FDCAN_Start(&hfdcan1);
	//can1 filter config begin
    
    //fdcan2 filter config begin
	//imu supercap
    can_filter.IdType = FDCAN_STANDARD_ID;//标准帧
    can_filter.FilterIndex = 0;
    can_filter.FilterType = FDCAN_FILTER_DUAL;//等于过滤
    can_filter.FilterID1 = IMU_ALL_ID;
    can_filter.FilterID2 = SUPERCAP_DATA_ID;
    can_filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;//通过过滤后给邮箱0
	HAL_FDCAN_ConfigFilter(&hfdcan2, &can_filter);
	//board commn
    can_filter.IdType = FDCAN_STANDARD_ID;//标准帧
    can_filter.FilterIndex = 1;
    can_filter.FilterType = FDCAN_FILTER_DUAL;//等于过滤
    can_filter.FilterID1 = FDCAN_GIMBAL_TO_CHA_ID;
	can_filter.FilterID2 = 0x016;
    can_filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO1;//通过过滤后给邮箱1
    HAL_FDCAN_ConfigFilter(&hfdcan2, &can_filter);  
	
	HAL_FDCAN_ConfigGlobalFilter(&hfdcan2, FDCAN_REJECT, FDCAN_REJECT, FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE);
    HAL_FDCAN_ActivateNotification(&hfdcan2, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0);//使能邮箱0新消息中断
    HAL_FDCAN_ActivateNotification(&hfdcan2, FDCAN_IT_RX_FIFO1_NEW_MESSAGE, 0);//使能邮箱1新消息中断
    HAL_FDCAN_Start(&hfdcan2);
	//fdcan2 filter config finish

    //can3 filter config begin
	//wheel motor
	can_filter.IdType = FDCAN_STANDARD_ID;//STANDARD
    can_filter.FilterIndex = 0;
    can_filter.FilterType = FDCAN_FILTER_DUAL;//DUAL
    can_filter.FilterID1 = DRIVER_MOTOR_LEFT_ID;
    can_filter.FilterID2 = DRIVER_MOTOR_RIGHT_ID;
    can_filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;//To Fifo0
    HAL_FDCAN_ConfigFilter(&hfdcan3, &can_filter);
	//yaw_motor trigger_motor
	can_filter.IdType = FDCAN_STANDARD_ID;//标准帧
    can_filter.FilterIndex = 1;
    can_filter.FilterType = FDCAN_FILTER_DUAL;//等于过滤
    can_filter.FilterID1 = YAW_MOTOR_ID;
    can_filter.FilterID2 = TRIGGER_MOTOR_ID;
    can_filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO1;//通过过滤后给邮箱1
	HAL_FDCAN_ConfigFilter(&hfdcan3, &can_filter);
		
    HAL_FDCAN_ConfigGlobalFilter(&hfdcan3, FDCAN_REJECT, FDCAN_REJECT, FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE);
    HAL_FDCAN_ActivateNotification(&hfdcan3, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0);//使能邮箱0新消息中断
    HAL_FDCAN_ActivateNotification(&hfdcan3, FDCAN_IT_RX_FIFO1_NEW_MESSAGE, 0);//使能邮箱1新消息中断
    HAL_FDCAN_Start(&hfdcan3);
	//can3 filter config finish
    
    //can tx_message config
    can_tx_message.IdType = FDCAN_STANDARD_ID;  
    can_tx_message.TxFrameType = FDCAN_DATA_FRAME;
    can_tx_message.DataLength = FDCAN_DLC_BYTES_8;
    can_tx_message.ErrorStateIndicator = FDCAN_ESI_PASSIVE;
    can_tx_message.BitRateSwitch = FDCAN_BRS_OFF;
    can_tx_message.FDFormat = FDCAN_CLASSIC_CAN;
    can_tx_message.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    can_tx_message.MessageMarker = 0;
	//fdcan tx_message config
	fdcan_tx_message.IdType = FDCAN_STANDARD_ID;  
    fdcan_tx_message.TxFrameType = FDCAN_DATA_FRAME;
    fdcan_tx_message.DataLength = FDCAN_DLC_BYTES_64;
    fdcan_tx_message.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    fdcan_tx_message.BitRateSwitch = FDCAN_BRS_ON;
    fdcan_tx_message.FDFormat = FDCAN_FD_CAN;
    fdcan_tx_message.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    fdcan_tx_message.MessageMarker = 0;
	
    //driver init 0.01993943
	dm_motor_init(&joint_motor[0], CAN_CHANNEL_1, JOINT_LB_CMD_ID, 2.91959381f, JOINT_LB_REC_ID);//LB -0.27579996 -0.313300014 4.4036479
	dm_motor_init(&joint_motor[1], CAN_CHANNEL_1, JOINT_LS_CMD_ID, 0.371440589f, JOINT_LS_REC_ID);//LS 0.564670026 5.52884054 3.90108585
	dm_motor_init(&joint_motor[2], CAN_CHANNEL_1, JOINT_RB_CMD_ID, 6.04196119f, JOINT_RB_REC_ID);//RB 4.47119999 4.52866888			1.68932343 1.63639832
	dm_motor_init(&joint_motor[3], CAN_CHANNEL_1, JOINT_RS_CMD_ID, 2.43732119f, JOINT_RS_REC_ID);//RS 2.62299991 2.3627491			2.42180824 2.44345903
	
	dji_motor_init(&driver_motor[0], DJI_3508_MOTOR, CAN_CHANNEL_3, DRIVER_MOTOR_LEFT_ID , DJI_3508_WHEEL_TAURUS_REDUCTION_RATIO);
    dji_motor_init(&driver_motor[1], DJI_3508_MOTOR, CAN_CHANNEL_3, DRIVER_MOTOR_RIGHT_ID, DJI_3508_WHEEL_TAURUS_REDUCTION_RATIO);
	dji_motor_init(&yaw_motor, 		 DJI_6020_MOTOR, CAN_CHANNEL_3, YAW_MOTOR_ID    	 , DJI_6020_ORIGINAL_REDUCTION_RATIO);
    // dji_motor_init(&trigger_motor,   DJI_2006_MOTOR, CAN_CHANNEL_3, TRIGGER_MOTOR_ID	 , DJI_2006_ORIGINAL_REDUCTION_RATIO);
    lk_motor_init(&trigger_motor, CAN_CHANNEL_3, TRIGGER_MOTOR_ID, 0.0f);
}

/*
 * @brief  邮箱0接收回调函数
 * @retval void
 * @note   在其中添加各驱动的数据接收函数
 */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    if((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != RESET) {
        if (hfdcan->Instance == FDCAN1) {
			HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &rx_fifo0_message, rx_fifo0_data);
			dm_motor_get_data(rx_fifo0_message.Identifier, rx_fifo0_data);
        } else if (hfdcan->Instance == FDCAN2) {
			HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &fdcan_rx_fifo0_message, fdcan_rx_fifo0_data);
			switch(fdcan_rx_fifo0_message.Identifier)
			{
				case IMU_ALL_ID :{imu_get_data(&chassis_imu, fdcan_rx_fifo0_message.Identifier, fdcan_rx_fifo0_data);break;}
				case SUPERCAP_DATA_ID :{power_get_data(fdcan_rx_fifo0_data);r_cnt++;break;}
				default : break;
			}
        } else if (hfdcan->Instance == FDCAN3) {
			HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &rx_fifo0_message, rx_fifo0_data);
			dji_motor_get_data(CAN_CHANNEL_3, rx_fifo0_message.Identifier, rx_fifo0_data);
        }
        HAL_FDCAN_ActivateNotification(hfdcan, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0);
    }
}

/*
 * @brief  邮箱1接收回调函数
 * @retval void
 * @note   在其中添加各驱动的数据接收函数
 */
void HAL_FDCAN_RxFifo1Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo1ITs)
{
    if((RxFifo1ITs & FDCAN_IT_RX_FIFO1_NEW_MESSAGE) != RESET) {
        if (hfdcan->Instance == FDCAN1) {
			HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO1, &rx_fifo1_message, rx_fifo1_data);
			dm_motor_get_data(rx_fifo1_message.Identifier, rx_fifo1_data);
        } else if (hfdcan->Instance == FDCAN2) {
			HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO1, &fdcan_rx_fifo1_message, fdcan_rx_fifo1_data);
			if(fdcan_rx_fifo1_message.Identifier == 0x016)
				imu_get_data(&gimbal_imu, fdcan_rx_fifo1_message.Identifier, fdcan_rx_fifo1_data);
			else
				fdcan_board_comm_get(fdcan_rx_fifo1_message.Identifier, fdcan_rx_fifo1_data);
        } else if (hfdcan->Instance == FDCAN3) {
			HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO1, &rx_fifo1_message, rx_fifo1_data);
			if(rx_fifo1_message.Identifier == TRIGGER_MOTOR_ID)
				lk_get_single_data(&trigger_motor,rx_fifo1_data);
			else
				dji_motor_get_data(CAN_CHANNEL_3, rx_fifo1_message.Identifier, rx_fifo1_data);	
			if(rx_fifo1_message.Identifier == YAW_MOTOR_ID)
				memcpy(fdcan_board_comm.tx_msg.e.gimbal_data.yaw_raw_data,rx_fifo1_data,8);
        }
        HAL_FDCAN_ActivateNotification(hfdcan, FDCAN_IT_RX_FIFO1_NEW_MESSAGE, 0);
    }
}

/*
 * @brief     can发送标准数据统一接口，提供给其它文件调用，8字节数据长度
 * @param[in] can_periph: can通道
 * @param[in] id        : 帧id
 * @param[in] data      : 数据指针
 * @retval    v oid
 */
void can_std_transmit(can_channel_e can_periph, uint32_t id, uint8_t *data)
{
    if (can_periph == CAN_CHANNEL_1)
	{
		can_tx_message.Identifier = id;
        HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &can_tx_message, data);
	}
    else if (can_periph == CAN_CHANNEL_2) 
	{
		fdcan_tx_message.Identifier = id;
        HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan2, &fdcan_tx_message, data);
	}
    else if (can_periph == CAN_CHANNEL_3)
	{
		can_tx_message.Identifier = id;
        HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan3, &can_tx_message, data);
	}
}
