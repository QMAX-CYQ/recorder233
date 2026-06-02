#ifndef __CAN_COMM_H
#define __CAN_COMM_H

#include "stm32h7xx.h"

typedef enum
{
    CAN_CHANNEL_1 = 0,
    CAN_CHANNEL_2 = 1,
    CAN_CHANNEL_3 = 2,
    CAN_CHANNEL_NUM = 3
} can_channel_e;

extern uint8_t yaw_raw_data[];

void can_comm_init(void);
void can_std_transmit(can_channel_e can_periph, uint32_t id, uint8_t *data);

#endif
