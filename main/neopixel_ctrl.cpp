#include "neopixel_ctrl.h"

#include "Adafruit_NeoPixel.h"

#include "esp_check.h"

#define NEOPIXEL_CTRL_GPIO      1
#define NEOPIXEL_CTRL_LED_COUNT 12

static Adafruit_NeoPixel s_pixels(NEOPIXEL_CTRL_LED_COUNT, NEOPIXEL_CTRL_GPIO, NEO_GRB + NEO_KHZ800);
static bool s_initialized = false;

extern "C" esp_err_t neopixel_ctrl_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_pixels.begin();
    s_pixels.clear();
    s_pixels.show();

    s_initialized = true;
    return ESP_OK;
}

extern "C" esp_err_t neopixel_ctrl_set_all_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, "neopixel_ctrl", "neopixel not initialized");

    for (uint16_t i = 0; i < NEOPIXEL_CTRL_LED_COUNT; ++i) {
        s_pixels.setPixelColor(i, s_pixels.Color(r, g, b));
    }
    s_pixels.show();

    return ESP_OK;
}
