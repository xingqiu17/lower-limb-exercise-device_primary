#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_sleep.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LP_POWER_KEY_DEFAULT_GPIO GPIO_NUM_13

typedef struct {
    esp_sleep_wakeup_cause_t wakeup_cause;
    int64_t slept_ms;
} lp_sleep_result_t;

esp_err_t lp_power_key_init(gpio_num_t gpio_num);

bool lp_power_key_poll_short_press(uint32_t min_press_ms, uint32_t max_press_ms);

esp_err_t lp_register_gpio_wakeup(void);

void lp_wait_power_key_inactive(void);

esp_err_t lp_enter_light_sleep(lp_sleep_result_t *result);

#ifdef __cplusplus
}
#endif
