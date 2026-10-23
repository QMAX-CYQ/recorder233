#ifndef __LED_CONTROL_H
#define __LED_CONTROL_H

#include "recorder.h"

void led_control_start(void);
void led_control_stop(void);
void led_pwm_generation(uint32_t channel, uint32_t brightness);
/* Publish SD state from the recorder task; no PWM access here. */
void SD_led_control(FRESULT sd_state, file_state_t file_state);
/* Call both functions below from one task every 10 ms. */
void led_control_update(void);
/* Before link readiness, breathe. rx_count counts packets containing log data. */
void COMM_led_control(uint8_t ready, uint32_t rx_count);

#endif
