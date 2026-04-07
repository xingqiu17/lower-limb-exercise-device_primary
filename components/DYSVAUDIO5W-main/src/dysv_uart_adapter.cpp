#include "dysv_uart_adapter.h"

#include <cstring>

#include "DYSVAudio5W.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {

static const char *TAG = "dysv_uart";

static constexpr uart_port_t DYSV_UART_PORT = UART_NUM_1;
static constexpr int DYSV_UART_TX_PIN = GPIO_NUM_17;
static constexpr int DYSV_UART_RX_PIN = GPIO_NUM_18;
static constexpr int DYSV_UART_BAUD_RATE = 9600;
static constexpr int DYSV_UART_BUF_SIZE = 256;

class IDFUartStream : public Stream {
public:
    IDFUartStream() = default;

    esp_err_t begin(uart_port_t port, int baud_rate, int tx_pin, int rx_pin)
    {
        port_ = port;

        uart_config_t uart_config;
        memset(&uart_config, 0, sizeof(uart_config));
        uart_config.baud_rate = baud_rate;
        uart_config.data_bits = UART_DATA_8_BITS;
        uart_config.parity = UART_PARITY_DISABLE;
        uart_config.stop_bits = UART_STOP_BITS_1;
        uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        uart_config.rx_flow_ctrl_thresh = 0;
        uart_config.source_clk = UART_SCLK_DEFAULT;

        ESP_RETURN_ON_ERROR(uart_driver_install(port_, DYSV_UART_BUF_SIZE, DYSV_UART_BUF_SIZE, 0, nullptr, 0), TAG, "uart driver install failed");
        ESP_RETURN_ON_ERROR(uart_param_config(port_, &uart_config), TAG, "uart param config failed");
        ESP_RETURN_ON_ERROR(uart_set_pin(port_, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "uart set pin failed");
        return ESP_OK;
    }

    int available() override
    {
        size_t length = 0;
        if (uart_get_buffered_data_len(port_, &length) != ESP_OK) {
            return 0;
        }
        return static_cast<int>(length);
    }

    int read() override
    {
        uint8_t byte = 0;
        int read_len = uart_read_bytes(port_, &byte, 1, 0);
        return (read_len == 1) ? static_cast<int>(byte) : -1;
    }

    size_t write(uint8_t value) override
    {
        return write(&value, 1);
    }

    size_t write(const uint8_t *buffer, size_t size) override
    {
        if (buffer == nullptr || size == 0) {
            return 0;
        }

        int written = uart_write_bytes(port_, reinterpret_cast<const char *>(buffer), size);
        return (written > 0) ? static_cast<size_t>(written) : 0;
    }

    void flush() override
    {
        (void)uart_wait_tx_done(port_, pdMS_TO_TICKS(100));
    }

private:
    uart_port_t port_ = UART_NUM_1;
};

class NullStream : public Stream {
public:
    int available() override { return 0; }
    int read() override { return -1; }
    size_t write(uint8_t) override { return 1; }
    size_t write(const uint8_t *, size_t size) override { return size; }
    void flush() override {}
};

static IDFUartStream s_uart_stream;
static NullStream s_null_stream;
static DYSVAudio5W s_player(s_uart_stream, DYSV_UART_BAUD_RATE, s_null_stream);
static SemaphoreHandle_t s_lock = nullptr;
static bool s_initialized = false;

static esp_err_t lock_take(void)
{
    if (s_lock == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void lock_give(void)
{
    if (s_lock != nullptr) {
        xSemaphoreGive(s_lock);
    }
}

} // namespace

extern "C" esp_err_t dysv_uart_adapter_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    if (s_lock == nullptr) {
        s_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_lock != nullptr, ESP_ERR_NO_MEM, TAG, "create mutex failed");
    }

    ESP_RETURN_ON_ERROR(s_uart_stream.begin(DYSV_UART_PORT, DYSV_UART_BAUD_RATE, DYSV_UART_TX_PIN, DYSV_UART_RX_PIN), TAG, "init uart stream failed");

    ESP_RETURN_ON_ERROR(lock_take(), TAG, "lock failed");
    (void)s_player.begin();
    lock_give();

    s_initialized = true;
    ESP_LOGI(TAG, "DYSV UART adapter init done, UART1 TX=%d RX=%d baud=%d", DYSV_UART_TX_PIN, DYSV_UART_RX_PIN, DYSV_UART_BAUD_RATE);
    return ESP_OK;
}

extern "C" esp_err_t dysv_uart_adapter_play_track(uint8_t song_number)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "adapter not initialized");
    ESP_RETURN_ON_FALSE(song_number > 0, ESP_ERR_INVALID_ARG, TAG, "invalid song number");
    ESP_RETURN_ON_ERROR(lock_take(), TAG, "lock failed");
    s_player.playTrack(song_number);
    lock_give();
    return ESP_OK;
}

extern "C" esp_err_t dysv_uart_adapter_stop_playback(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "adapter not initialized");
    ESP_RETURN_ON_ERROR(lock_take(), TAG, "lock failed");
    s_player.stopPlayback();
    lock_give();
    return ESP_OK;
}

extern "C" int dysv_uart_adapter_get_playback_state(void)
{
    if (!s_initialized) {
        return -1;
    }

    if (lock_take() != ESP_OK) {
        return -1;
    }

    int state = s_player.getPlaybackState();
    lock_give();

    if (state != 0 && state != 1) {
        return -1;
    }
    return state;
}
