#include "led_control.h"
#include "tim.h"
#define SD_LED_CHANNEL TIM_CHANNEL_1
#define COMM_LED_CHANNEL TIM_CHANNEL_2
#define BREATH_HALF_PERIOD_MS 1000U
#define COMM_BLINK_HALF_PERIOD_MS 250U
#define COMM_ACTIVITY_HOLD_MS 1000U

enum { SD_LED_OFF, SD_LED_ON, SD_LED_BREATH };
static volatile uint32_t sd_led_mode = SD_LED_OFF;
static uint8_t led_running;
static uint8_t sd_breathing;
static uint32_t sd_breath_tick;
static uint8_t comm_breathing;
static uint32_t comm_breath_tick;
static uint8_t comm_active;
static uint32_t comm_rx_count;
static uint32_t comm_last_rx_tick;
static uint32_t comm_blink_tick;

static uint32_t led_full_brightness(void)
{
    return __HAL_TIM_GET_AUTORELOAD(&htim2) + 1U;
}

static uint32_t led_breath_brightness(uint32_t elapsed)
{
    uint32_t phase = elapsed % (2U * BREATH_HALF_PERIOD_MS);
    /* Enter at full brightness; square the ramp for visual smoothness. */
    uint32_t level = phase <= BREATH_HALF_PERIOD_MS ?
        BREATH_HALF_PERIOD_MS - phase : phase - BREATH_HALF_PERIOD_MS;
    return (uint32_t)((uint64_t)led_full_brightness() * level * level /
                      (BREATH_HALF_PERIOD_MS * BREATH_HALF_PERIOD_MS));
}

void led_control_start(void)
{
    if (led_running) return;
    sd_breathing = 0U;
    comm_breathing = 0U;
    comm_active = 0U;
    led_pwm_generation(SD_LED_CHANNEL, 0U);
    led_pwm_generation(COMM_LED_CHANNEL, 0U);
    HAL_TIM_PWM_Start(&htim2, SD_LED_CHANNEL);
    HAL_TIM_PWM_Start(&htim2, COMM_LED_CHANNEL);
    led_running = 1U;
}

void led_control_stop(void)
{
    led_running = 0U;
    led_pwm_generation(SD_LED_CHANNEL, 0U);
    led_pwm_generation(COMM_LED_CHANNEL, 0U);
    HAL_TIM_PWM_Stop(&htim2, SD_LED_CHANNEL);
    HAL_TIM_PWM_Stop(&htim2, COMM_LED_CHANNEL);
}

void led_pwm_generation(uint32_t channel, uint32_t brightness)
{
    uint32_t full = led_full_brightness();
    if ((channel != SD_LED_CHANNEL) && (channel != COMM_LED_CHANNEL)) return;
    if (brightness > full) brightness = full;
    __HAL_TIM_SET_COMPARE(&htim2, channel, brightness);
}

void SD_led_control(FRESULT sd_state, file_state_t file_state)
{
    sd_led_mode = sd_state != FR_OK ? SD_LED_OFF :
        (file_state == RS_READY ? SD_LED_BREATH : SD_LED_ON);
}

void led_control_update(void)
{
    uint32_t mode = sd_led_mode;
    uint32_t now = HAL_GetTick();
    uint32_t full = led_full_brightness();

    if (!led_running) return;
    if (mode != SD_LED_BREATH) {
        sd_breathing = 0U;
        led_pwm_generation(SD_LED_CHANNEL, mode == SD_LED_ON ? full : 0U);
        return;
    }
    if (!sd_breathing) {
        sd_breathing = 1U;
        sd_breath_tick = now;
    }
    led_pwm_generation(SD_LED_CHANNEL, led_breath_brightness(now - sd_breath_tick));
}

void COMM_led_control(uint8_t ready, uint32_t rx_count)
{
    uint32_t now = HAL_GetTick();
    uint32_t brightness = led_full_brightness();

    if (!led_running) return;
    if (!ready) {
        comm_active = 0U;
        comm_rx_count = rx_count;
        if (!comm_breathing) {
            comm_breathing = 1U;
            comm_breath_tick = now;
        }
        led_pwm_generation(COMM_LED_CHANNEL, led_breath_brightness(now - comm_breath_tick));
        return;
    }
    comm_breathing = 0U;
    if (rx_count != comm_rx_count) {
        comm_rx_count = rx_count;
        comm_last_rx_tick = now;
        if (!comm_active) {
            comm_active = 1U;
            comm_blink_tick = now;
        }
    }
    if (comm_active && (uint32_t)(now - comm_last_rx_tick) >= COMM_ACTIVITY_HOLD_MS)
        comm_active = 0U;
    /* Packets extend activity without restarting the blink phase. */
    if (comm_active && ((now - comm_blink_tick) / COMM_BLINK_HALF_PERIOD_MS) % 2U == 0U)
        brightness = 0U;
    led_pwm_generation(COMM_LED_CHANNEL, brightness);
}


