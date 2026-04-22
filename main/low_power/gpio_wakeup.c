#include "gpio_wakeup.h"

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LP_POWER_KEY_SCAN_MS 20

static const char *TAG = "low_power";

static gpio_num_t s_power_key_gpio = LP_POWER_KEY_DEFAULT_GPIO;
static int s_prev_level = 1;
static bool s_pressed = false;
static TickType_t s_pressed_tick = 0;

esp_err_t lp_power_key_init(gpio_num_t gpio_num)
{
    if (gpio_num < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    s_power_key_gpio = gpio_num;

    gpio_config_t cfg = {
        .pin_bit_mask = BIT64((uint32_t)s_power_key_gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "config power key failed");

    s_prev_level = gpio_get_level(s_power_key_gpio);
    s_pressed = false;
    s_pressed_tick = 0;

    ESP_LOGI(TAG, "power key ready gpio=%d", (int)s_power_key_gpio);
    return ESP_OK;
}

bool lp_power_key_poll_short_press(uint32_t min_press_ms, uint32_t max_press_ms)
{
    int current_level = gpio_get_level(s_power_key_gpio);

    if (!s_pressed && s_prev_level == 1 && current_level == 0) {
        s_pressed = true;
        s_pressed_tick = xTaskGetTickCount();
    } else if (s_pressed && s_prev_level == 0 && current_level == 1) {
        TickType_t now = xTaskGetTickCount();
        uint32_t pressed_ms = (uint32_t)((now - s_pressed_tick) * portTICK_PERIOD_MS);
        s_pressed = false;

        if ((pressed_ms >= min_press_ms) && (pressed_ms <= max_press_ms)) {
            s_prev_level = current_level;
            vTaskDelay(pdMS_TO_TICKS(LP_POWER_KEY_SCAN_MS));
            return true;
        }
    }

    s_prev_level = current_level;
    vTaskDelay(pdMS_TO_TICKS(LP_POWER_KEY_SCAN_MS));
    return false;
}

esp_err_t lp_register_gpio_wakeup(void)
{
    ESP_RETURN_ON_ERROR(gpio_wakeup_enable(s_power_key_gpio, GPIO_INTR_LOW_LEVEL), TAG, "gpio wakeup enable failed");
    ESP_RETURN_ON_ERROR(esp_sleep_enable_gpio_wakeup(), TAG, "sleep gpio wakeup enable failed");
    return ESP_OK;
}

void lp_wait_power_key_inactive(void)
{
    while (gpio_get_level(s_power_key_gpio) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t lp_enter_light_sleep(lp_sleep_result_t *result)
{
    ESP_RETURN_ON_ERROR(lp_register_gpio_wakeup(), TAG, "register wakeup failed");

    int64_t t0 = esp_timer_get_time();
    ESP_LOGI(TAG, "enter light sleep");
    uart_wait_tx_idle_polling(CONFIG_ESP_CONSOLE_UART_NUM);

    esp_err_t ret = esp_light_sleep_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_light_sleep_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    int64_t t1 = esp_timer_get_time();
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (result != NULL) {
        result->wakeup_cause = cause;
        result->slept_ms = (t1 - t0) / 1000;
    }

    ESP_LOGI(TAG, "exit light sleep cause=%d slept=%lldms", (int)cause, (long long)((t1 - t0) / 1000));

    if (cause == ESP_SLEEP_WAKEUP_GPIO) {
        lp_wait_power_key_inactive();
    }

    return ESP_OK;
}
