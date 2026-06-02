#ifndef __PROT_MS53L0M_H
#define __PROT_MS53L0M_H

#include <stdint.h>

#define MS53L0M_NORMAL_RX_BUF_SIZE 64

extern volatile uint16_t ms53l0m_distance_mm[2];
extern volatile float ms53l0m_distance_m[2];
extern volatile uint8_t ms53l0m_data_valid[2];

uint8_t ms53l0m_normal_get_data(uint8_t *buff, uint16_t len, uint8_t side);

#endif
