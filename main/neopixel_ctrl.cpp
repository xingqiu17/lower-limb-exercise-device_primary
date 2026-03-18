#include "neopixel_ctrl.h"

#include "Adafruit_NeoPixel.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define NEOPIXEL_CTRL_GPIO1 1
#define NEOPIXEL_CTRL_GPIO2 2

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
    s_pixels_gpio1.clear();
    s_pixels_gpio1.show();

    s_pixels_gpio2.begin();
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

extern "C" uint16_t neopixel_ctrl_get_led_count(void)
{
    return NEOPIXEL_CTRL_LED_COUNT;
}
