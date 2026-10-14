#include "board_comm_task.h"
#include "cmsis_os.h"
#include <string.h>
#include "board_comm.h"
#include "container.h"
#include "container_bus.h"
#include "can_comm.h"

static void rc_data_cb(uint32_t tag_id, void* data, size_t len) {
	if(data == NULL || len != sizeof(rc_data_t))
		return;
	memcpy(&fdcan_board_comm.tx_msg.e.rc_data,(rc_data_t*)data,len);
}

static void kb_data_cb(uint32_t tag_id, void* data, size_t len) {
	if(data == NULL || len != sizeof(kb_data_t))
		return;
	memcpy(&fdcan_board_comm.tx_msg.e.kb_data,(kb_data_t*)data,len);
}

static void imu_data_cb(uint32_t tag_id, void* data, size_t len) {
	if(data == NULL || len != sizeof(imu_data_t))
		return;
	memcpy(&fdcan_board_comm.tx_msg.e.imu_data,(imu_data_t*)data,len);
}

static void judge_data_cb(uint32_t tag_id, void* data, size_t len) {
	if(data == NULL || len != sizeof(judge_data_t))
		return;
	memcpy(&fdcan_board_comm.tx_msg.e.judge_data,(judge_data_t*)data,len);
}

static void vision_data_cb(uint32_t tag_id, void* data, size_t len) {
	if(data == NULL || len != sizeof(vision_tx_data_t))
		return;
	memcpy(&fdcan_board_comm.tx_msg.e.vision_data,(vision_tx_data_t*)data,len);
}

static void gimbal_data_cb(uint32_t tag_id, void* data, size_t len) {
	if(data == NULL || len != sizeof(gimbal_tx_data_t))
		return;
	memcpy(&fdcan_board_comm.tx_msg.e.gimbal_data,(gimbal_tx_data_t*)data,len);
}

static void transmit_data_cb(uint32_t tag_id, void* data, size_t len) {
	if(data == NULL || len != FDCAN_BOARD_DATA_LEN)
		return;
	uint8_t *fdcan_tx_buff = (uint8_t*)data;
    can_std_transmit(CAN_CHANNEL_2,FDCAN_CHA_TO_GIMBAL_ID,fdcan_tx_buff);
}

// --- ªÿµ˜≈‰÷√±Ì  ---
static const ContainerBusCfg mb_callback[] = {
	{ TAG_DR16_RC_DATA, 	  rc_data_cb, 		NULL },
	{ TAG_DR16_KB_DATA, 	  kb_data_cb, 		NULL },
	{ TAG_CHA_IMU_DATA, 	  imu_data_cb, 		NULL },
	{ TAG_JUDGE_DATA,  		 judge_data_cb, 	NULL },
	{ TAG_SHOOT_VISION_DATA, vision_data_cb,	NULL },
    { TAG_GIMBAL_CTRL_DATA, gimbal_data_cb, 	NULL },
	{ TAG_TRANSMIT_DATA, 	  transmit_data_cb, NULL },
};

void board_comm_task(const void *argu)
{
	container_bus_init(mb_callback, sizeof(mb_callback)/sizeof(ContainerBusCfg));
	uint32_t thread_wake_time = osKernelSysTick();
	for(;;)
	{
		thread_wake_time = osKernelSysTick();
		
		board_comm_container_set();//set container

		osDelayUntil(&thread_wake_time,2);
	}
}



