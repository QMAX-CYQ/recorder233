#include "board_comm.h"
#include "string.h"
#include "container.h"
#include "can_comm.h"

fdcan_board_comm_t fdcan_board_comm;

data_remote_t data_remote_rec;
data_keyboard_t data_keyboard_rec;
gimbal_data_t gimbal_data_rec;
vision_data_t vision_data_rec;

rc_data_t rc_data_tran;
kb_data_t kb_data_tran;
imu_data_t imu_data_tran;
judge_data_t judge_data_tran;
vision_tx_data_t vision_data_tran;
gimbal_tx_data_t gimbal_data_tran;

uint8_t board_comm_online;

//fdcan板间通信收数据
void fdcan_board_comm_get(uint32_t id,uint8_t *pdata)
{
	if(id == FDCAN_GIMBAL_TO_CHA_ID)
	{
		//联合体内存拷贝
		memcpy(&fdcan_board_comm.rx_msg.buff,pdata,FDCAN_BOARD_DATA_LEN);
		
		memcpy(&data_remote_rec,&fdcan_board_comm.rx_msg.e.data_remote,sizeof(data_remote_rec));
		
		memcpy(&data_keyboard_rec,&fdcan_board_comm.rx_msg.e.data_keyboard,sizeof(data_keyboard_rec));

		memcpy(&gimbal_data_rec,&fdcan_board_comm.rx_msg.e.gimbal_data,sizeof(gimbal_data_rec));

		memcpy(&vision_data_rec,&fdcan_board_comm.rx_msg.e.vision_data,sizeof(vision_data_rec));
		
		board_comm_online = 1;
	}
}

void board_comm_container_set(void)
{
	//收到的所有消息推入
	container_set(TAG_VTM_REMOTE_DATA,   	&data_remote_rec,   sizeof(data_remote_rec),   CONTAINER_TYPE_STRUCT);
	container_set(TAG_VTM_KEYBOARD_DATA, 	&data_keyboard_rec, sizeof(data_keyboard_rec), CONTAINER_TYPE_STRUCT);
	container_set(TAG_GIMBAL_OUTPUT_DATA,   &gimbal_data_rec,   sizeof(gimbal_data_rec),   CONTAINER_TYPE_STRUCT);
	container_set(TAG_TRACE_VISION_DATA,   	&vision_data_rec,   sizeof(vision_data_rec),   CONTAINER_TYPE_STRUCT);
	//所有待发送信息打包
	container_set(TAG_TRANSMIT_DATA, 		fdcan_board_comm.tx_msg.buff, FDCAN_BOARD_DATA_LEN,CONTAINER_TYPE_INT);
}

uint8_t board_comm_check_offline(void)
{
    if (board_comm_online == 0) {
        return 1;
    } else {
        board_comm_online = 0;
        return 0;
    }
}
