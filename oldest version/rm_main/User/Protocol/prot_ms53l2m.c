#include "prot_ms53l2m.h"

#include <stddef.h>

volatile uint16_t ms53l2m_distance_mm;
volatile float ms53l2m_distance_m;
volatile uint8_t ms53l2m_data_valid;

uint8_t ms53l2m_normal_get_data(uint8_t *buff, uint16_t len)
{
    uint16_t i;
    uint16_t distance = 0;
    uint8_t found_digit = 0;

    ms53l2m_data_valid = 0;

    if (buff == NULL || len == 0) {
        return 1;
    }

    for (i = 0; i < len; i++) {
        if (buff[i] >= '0' && buff[i] <= '9') {
            uint16_t next_distance = (uint16_t)(distance * 10u + (uint16_t)(buff[i] - '0'));

            if (next_distance < distance) {
                return 1;
            }

            distance = next_distance;
            found_digit = 1;
        } else if (buff[i] == '\r' || buff[i] == '\n' || buff[i] == '\0') {
            if (found_digit == 0) {
                return 1;
            }

            ms53l2m_distance_mm = distance;
            ms53l2m_distance_m = (float)distance * 0.001f;
            ms53l2m_data_valid = 1;
            return 0;
        }
    }

    return 1;
}
