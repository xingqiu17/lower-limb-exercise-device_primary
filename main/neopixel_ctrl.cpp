#include "neopixel_ctrl.h"

#include "Adafruit_NeoPixel.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define NEOPIXEL_CTRL_GPIO1 1
#define NEOPIXEL_CTRL_GPIO2 2
#define NEOPIXEL_CTRL_BRIGHTNESS_50PCT 128U
#define NEOPIXEL_CTRL_GPIO2_USED_LED_COUNT 6U
#define NEOPIXEL_CTRL_GPIO2_INDICATOR_COUNT 2U
#define NEOPIXEL_CTRL_GPIO2_STATUS_START_INDEX 2U
#define NEOPIXEL_CTRL_GPIO2_STATUS_COUNT 4U

static Adafruit_NeoPixel s_pixels_gpio1(NEOPIXEL_CTRL_LED_COUNT, NEOPIXEL_CTRL_GPIO1, NEO_GRB + NEO_KHZ800);
static Adafruit_NeoPixel s_pixels_gpio2(NEOPIXEL_CTRL_LED_COUNT, NEOPIXEL_CTRL_GPIO2, NEO_GRB + NEO_KHZ800);
static bool s_initialized = false;
static SemaphoreHandle_t s_pixels_lock = NULL;

static void neopixel_fill_all(Adafruit_NeoPixel &pixels, uint8_t r, uint8_t g, uint8_t b)
{
    for (uint16_t i = 0; i < NEOPIXEL_CTRL_LED_COUNT; ++i) {
        pixels.setPixelColor(i, pixels.Color(r, g, b));
    }
}

static inline void neopixel_gpio2_clear_used(void)
{
    for (uint16_t i = 0; i < NEOPIXEL_CTRL_GPIO2_USED_LED_COUNT; ++i) {
        s_pixels_gpio2.setPixelColor(i, 0);
    }
}

static inline bool neopixel_ctrl_lock(TickType_t ticks_to_wait)
{
    return (xSemaphoreTake(s_pixels_lock, ticks_to_wait) == pdTRUE);
}

static inline void neopixel_ctrl_unlock(void)
{
    xSemaphoreGive(s_pixels_lock);
}

extern "C" esp_err_t neopixel_ctrl_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_pixels_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_pixels_lock != NULL, ESP_ERR_NO_MEM, "neopixel_ctrl", "create mutex failed");

    s_pixels_gpio1.begin();
    s_pixels_gpio1.setBrightness(NEOPIXEL_CTRL_BRIGHTNESS_50PCT);
    s_pixels_gpio1.clear();
    s_pixels_gpio1.show();

    s_pixels_gpio2.begin();
    s_pixels_gpio2.setBrightness(NEOPIXEL_CTRL_BRIGHTNESS_50PCT);
    s_pixels_gpio2.clear();
    s_pixels_gpio2.show();

    s_initialized = true;
    return ESP_OK;
}

extern "C" esp_err_t neopixel_ctrl_clear_all(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, "neopixel_ctrl", "neopixel not initialized");

    ESP_RETURN_ON_FALSE(neopixel_ctrl_lock(pdMS_TO_TICKS(50)), ESP_ERR_TIMEOUT, "neopixel_ctrl", "mutex timeout");

    s_pixels_gpio1.clear();
    s_pixels_gpio1.show();

    s_pixels_gpio2.clear();
    s_pixels_gpio2.show();

    neopixel_ctrl_unlock();
    return ESP_OK;
}

extern "C" esp_err_t neopixel_ctrl_set_all_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, "neopixel_ctrl", "neopixel not initialized");

    ESP_RETURN_ON_FALSE(neopixel_ctrl_lock(pdMS_TO_TICKS(50)), ESP_ERR_TIMEOUT, "neopixel_ctrl", "mutex timeout");

    neopixel_fill_all(s_pixels_gpio1, r, g, b);
    s_pixels_gpio1.show();

    neopixel_ctrl_unlock();

    return ESP_OK;
}

extern "C" esp_err_t neopixel_ctrl_set_gpio1_progress_red(uint16_t lit_count)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, "neopixel_ctrl", "neopixel not initialized");

    if (lit_count > NEOPIXEL_CTRL_LED_COUNT) {
        lit_count = NEOPIXEL_CTRL_LED_COUNT;
    }

    ESP_RETURN_ON_FALSE(neopixel_ctrl_lock(pdMS_TO_TICKS(50)), ESP_ERR_TIMEOUT, "neopixel_ctrl", "mutex timeout");

    s_pixels_gpio1.clear();
    for (uint16_t i = 0; i < lit_count; ++i) {
        s_pixels_gpio1.setPixelColor(i, s_pixels_gpio1.Color(255, 0, 0));
    }
    s_pixels_gpio1.show();

    neopixel_ctrl_unlock();
    return ESP_OK;
}

extern "C" esp_err_t neopixel_ctrl_set_gpio2_all_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, "neopixel_ctrl", "neopixel not initialized");

    ESP_RETURN_ON_FALSE(neopixel_ctrl_lock(pdMS_TO_TICKS(50)), ESP_ERR_TIMEOUT, "neopixel_ctrl", "mutex timeout");

    neopixel_fill_all(s_pixels_gpio2, r, g, b);
    s_pixels_gpio2.show();

    neopixel_ctrl_unlock();
    return ESP_OK;
}

extern "C" esp_err_t neopixel_ctrl_set_gpio2_action_panel(uint8_t indicator1_on,
                                                            uint8_t indicator1_r,
                                                            uint8_t indicator1_g,
                                                            uint8_t indicator1_b,
                                                            uint8_t indicator2_on,
                                                            uint8_t indicator2_r,
                                                            uint8_t indicator2_g,
                                                            uint8_t indicator2_b,
                                                            uint8_t active_action,
                                                            uint8_t status_on)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, "neopixel_ctrl", "neopixel not initialized");

    ESP_RETURN_ON_FALSE(neopixel_ctrl_lock(pdMS_TO_TICKS(50)), ESP_ERR_TIMEOUT, "neopixel_ctrl", "mutex timeout");

    neopixel_gpio2_clear_used();

    if (indicator1_on != 0U) {
        s_pixels_gpio2.setPixelColor(0, s_pixels_gpio2.Color(indicator1_r, indicator1_g, indicator1_b));
    }

    if ((indicator2_on != 0U) && (NEOPIXEL_CTRL_GPIO2_INDICATOR_COUNT >= 2U)) {
        s_pixels_gpio2.setPixelColor(1, s_pixels_gpio2.Color(indicator2_r, indicator2_g, indicator2_b));
    }

    if ((status_on != 0U) && (active_action >= 1U) && (active_action <= NEOPIXEL_CTRL_GPIO2_STATUS_COUNT)) {
        uint16_t led_index = (uint16_t)(NEOPIXEL_CTRL_GPIO2_STATUS_START_INDEX + active_action - 1U);
        s_pixels_gpio2.setPixelColor(led_index, s_pixels_gpio2.Color(255, 0, 0));
    }

    s_pixels_gpio2.show();

    neopixel_ctrl_unlock();
    return ESP_OK;
}

extern "C" uint16_t neopixel_ctrl_get_led_count(void)
{
    return NEOPIXEL_CTRL_LED_COUNT;
}
