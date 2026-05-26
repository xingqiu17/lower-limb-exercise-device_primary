#include "neopixel_ctrl.h"

#include "Adafruit_NeoPixel.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define NEOPIXEL_CTRL_GPIO1 1
#define NEOPIXEL_CTRL_BRIGHTNESS_50PCT 128U
#define NEOPIXEL_CTRL_PROGRESS_START_INDEX 1U
#define NEOPIXEL_CTRL_PROGRESS_COUNT 10U
#define NEOPIXEL_CTRL_INDICATOR_START_INDEX 11U
#define NEOPIXEL_CTRL_INDICATOR_COUNT 2U

static Adafruit_NeoPixel s_pixels(NEOPIXEL_CTRL_LED_COUNT, NEOPIXEL_CTRL_GPIO1, NEO_GRB + NEO_KHZ800);
static bool s_initialized = false;
static SemaphoreHandle_t s_pixels_lock = NULL;

static void neopixel_fill_all(Adafruit_NeoPixel &pixels, uint8_t r, uint8_t g, uint8_t b)
{
    for (uint16_t i = 0; i < NEOPIXEL_CTRL_LED_COUNT; ++i) {
        pixels.setPixelColor(i, pixels.Color(r, g, b));
    }
}

static inline void neopixel_clear_progress_leds(void)
{
    for (uint16_t i = NEOPIXEL_CTRL_PROGRESS_START_INDEX;
         i < (NEOPIXEL_CTRL_PROGRESS_START_INDEX + NEOPIXEL_CTRL_PROGRESS_COUNT);
         ++i) {
        s_pixels.setPixelColor(i, 0);
    }
}

static inline void neopixel_clear_indicator_leds(void)
{
    for (uint16_t i = NEOPIXEL_CTRL_INDICATOR_START_INDEX;
         i < (NEOPIXEL_CTRL_INDICATOR_START_INDEX + NEOPIXEL_CTRL_INDICATOR_COUNT);
         ++i) {
        s_pixels.setPixelColor(i, 0);
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

    s_pixels.begin();
    s_pixels.setBrightness(NEOPIXEL_CTRL_BRIGHTNESS_50PCT);
    s_pixels.clear();
    s_pixels.show();

    s_initialized = true;
    return ESP_OK;
}

extern "C" esp_err_t neopixel_ctrl_clear_all(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, "neopixel_ctrl", "neopixel not initialized");

    ESP_RETURN_ON_FALSE(neopixel_ctrl_lock(pdMS_TO_TICKS(50)), ESP_ERR_TIMEOUT, "neopixel_ctrl", "mutex timeout");

    s_pixels.clear();
    s_pixels.show();

    neopixel_ctrl_unlock();
    return ESP_OK;
}

extern "C" esp_err_t neopixel_ctrl_set_all_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, "neopixel_ctrl", "neopixel not initialized");

    ESP_RETURN_ON_FALSE(neopixel_ctrl_lock(pdMS_TO_TICKS(50)), ESP_ERR_TIMEOUT, "neopixel_ctrl", "mutex timeout");

    neopixel_fill_all(s_pixels, r, g, b);
    s_pixels.show();

    neopixel_ctrl_unlock();

    return ESP_OK;
}

extern "C" esp_err_t neopixel_ctrl_set_gpio1_progress_red(uint16_t lit_count)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, "neopixel_ctrl", "neopixel not initialized");

    if (lit_count > NEOPIXEL_CTRL_PROGRESS_COUNT) {
        lit_count = NEOPIXEL_CTRL_PROGRESS_COUNT;
    }

    ESP_RETURN_ON_FALSE(neopixel_ctrl_lock(pdMS_TO_TICKS(50)), ESP_ERR_TIMEOUT, "neopixel_ctrl", "mutex timeout");

    s_pixels.setPixelColor(0, 0);
    neopixel_clear_progress_leds();
    for (uint16_t i = 0; i < lit_count; ++i) {
        s_pixels.setPixelColor((uint16_t)(NEOPIXEL_CTRL_PROGRESS_START_INDEX + i), s_pixels.Color(0, 0, 255));
    }
    s_pixels.show();

    neopixel_ctrl_unlock();
    return ESP_OK;
}

extern "C" esp_err_t neopixel_ctrl_set_gpio2_all_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, "neopixel_ctrl", "neopixel not initialized");

    ESP_RETURN_ON_FALSE(neopixel_ctrl_lock(pdMS_TO_TICKS(50)), ESP_ERR_TIMEOUT, "neopixel_ctrl", "mutex timeout");

    neopixel_fill_all(s_pixels, r, g, b);
    s_pixels.show();

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

    s_pixels.setPixelColor(0, 0);
    neopixel_clear_indicator_leds();

    if (indicator1_on != 0U) {
        s_pixels.setPixelColor(NEOPIXEL_CTRL_INDICATOR_START_INDEX,
                               s_pixels.Color(indicator1_r, indicator1_g, indicator1_b));
    }

    if ((indicator2_on != 0U) && (NEOPIXEL_CTRL_INDICATOR_COUNT >= 2U)) {
        s_pixels.setPixelColor((uint16_t)(NEOPIXEL_CTRL_INDICATOR_START_INDEX + 1U),
                               s_pixels.Color(indicator2_r, indicator2_g, indicator2_b));
    }

    (void)active_action;
    (void)status_on;

    s_pixels.show();

    neopixel_ctrl_unlock();
    return ESP_OK;
}

extern "C" uint16_t neopixel_ctrl_get_led_count(void)
{
    return NEOPIXEL_CTRL_LED_COUNT;
}
