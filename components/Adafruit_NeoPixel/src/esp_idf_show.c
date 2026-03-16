#include <stdint.h>

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "AdafruitNeoEsp";

#define ESP_SHOW_RESOLUTION_HZ 10000000

static rmt_channel_handle_t s_tx_channel = NULL;
static rmt_encoder_handle_t s_bytes_encoder = NULL;
static uint16_t s_rmt_pin = 0xFFFF;

static esp_err_t esp_show_create_rmt(uint16_t pin)
{
    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = pin,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = ESP_SHOW_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_chan_config, &s_tx_channel), TAG, "create tx channel failed");

    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = 4,
            .level1 = 0,
            .duration1 = 8,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = 8,
            .level1 = 0,
            .duration1 = 4,
        },
        .flags = {
            .msb_first = 1,
        },
    };
    ESP_RETURN_ON_ERROR(rmt_new_bytes_encoder(&bytes_encoder_config, &s_bytes_encoder), TAG, "create bytes encoder failed");

    ESP_RETURN_ON_ERROR(rmt_enable(s_tx_channel), TAG, "enable tx channel failed");
    s_rmt_pin = pin;

    return ESP_OK;
}

static esp_err_t esp_show_ensure_rmt(uint16_t pin)
{
    if ((s_tx_channel != NULL) && (s_bytes_encoder != NULL) && (s_rmt_pin == pin)) {
        return ESP_OK;
    }

    if (s_tx_channel != NULL) {
        rmt_disable(s_tx_channel);
        rmt_del_channel(s_tx_channel);
        s_tx_channel = NULL;
    }

    if (s_bytes_encoder != NULL) {
        rmt_del_encoder(s_bytes_encoder);
        s_bytes_encoder = NULL;
    }

    return esp_show_create_rmt(pin);
}

void espShow(uint16_t pin, uint8_t *pixels, uint32_t numBytes, uint8_t type)
{
    (void)type;

    if ((pixels == NULL) || (numBytes == 0)) {
        return;
    }

    esp_err_t ret = esp_show_ensure_rmt(pin);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ensure rmt failed: %s", esp_err_to_name(ret));
        return;
    }

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };

    ret = rmt_transmit(s_tx_channel, s_bytes_encoder, pixels, numBytes, &tx_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "rmt transmit failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = rmt_tx_wait_all_done(s_tx_channel, -1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "wait tx done failed: %s", esp_err_to_name(ret));
    }
}
