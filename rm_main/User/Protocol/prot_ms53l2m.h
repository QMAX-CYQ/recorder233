#ifndef __PROT_MS53L2M_H
#define __PROT_MS53L2M_H

#include <stdint.h>

#define MS53L2M_NORMAL_RX_BUF_SIZE 64

extern volatile uint16_t ms53l2m_distance_mm;
extern volatile float ms53l2m_distance_m;
extern volatile uint8_t ms53l2m_data_valid;

uint8_t ms53l2m_normal_get_data(uint8_t *buff, uint16_t len);

#endif
