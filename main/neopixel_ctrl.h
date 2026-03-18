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

uint16_t neopixel_ctrl_get_led_count(void);

#ifdef __cplusplus
}
#endif
