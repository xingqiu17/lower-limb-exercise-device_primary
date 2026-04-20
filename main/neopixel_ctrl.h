#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEOPIXEL_CTRL_LED_COUNT 12U

esp_err_t neopixel_ctrl_init(void);

esp_err_t neopixel_ctrl_set_all_rgb(uint8_t r, uint8_t g, uint8_t b);

esp_err_t neopixel_ctrl_clear_all(void);

esp_err_t neopixel_ctrl_set_gpio1_progress_red(uint16_t lit_count);

esp_err_t neopixel_ctrl_set_gpio2_all_rgb(uint8_t r, uint8_t g, uint8_t b);

esp_err_t neopixel_ctrl_set_gpio2_action_panel(uint8_t indicator1_on,
											   uint8_t indicator1_r,
											   uint8_t indicator1_g,
											   uint8_t indicator1_b,
											   uint8_t indicator2_on,
											   uint8_t indicator2_r,
											   uint8_t indicator2_g,
											   uint8_t indicator2_b,
											   uint8_t active_action,
											   uint8_t status_on);

uint16_t neopixel_ctrl_get_led_count(void);

#ifdef __cplusplus
}
#endif
