/* Get Start Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)

#endif

#include "espnow.h"
#include "espnow_storage.h"
#include "espnow_utils.h"

#include "audio_play.h"

#include "espnow_sr.h"
#include "state_machine.h"

static const char *TAG = "app_main";

#define AUDIO_BUSY_GPIO GPIO_NUM_8

master_state_t m_state = MASTER_IDLE;
slave_state_t s_state[2] = {SLAVE_IDLE,SLAVE_IDLE};

static void audio_play_test_task(void *arg)
{
    const uint8_t test_music_io = 1;

    vTaskDelay(pdMS_TO_TICKS(2000));

    while (true) {
        esp_err_t ret = audio_play_trigger_once(test_music_io, 100);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "trigger music IO%u ok", test_music_io);
        } else {
            ESP_LOGE(TAG, "trigger music IO%u failed: %s", test_music_io, esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void audio_play_event_task(void *arg)
{
    QueueHandle_t evt_queue = audio_play_get_event_queue();
    audio_play_event_t evt;

    if (evt_queue == NULL) {
        ESP_LOGE(TAG, "audio_play event queue is null");
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        if (xQueueReceive(evt_queue, &evt, portMAX_DELAY) == pdTRUE) {
            if (evt.type == AUDIO_PLAY_EVENT_BUSY_RISING) {
                ESP_LOGI(TAG, "recv BUSY rising event, tick=%lu", (unsigned long)evt.tick);
            }
        }
    }
}

static void busy_monitor_log_task(void *arg)
{
    while (true) {
        int busy_level = gpio_get_level(AUDIO_BUSY_GPIO);
        ESP_LOGI(TAG, "BUSY(IO18) level=%d", busy_level);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}




static void app_wifi_init()
{
    esp_event_loop_create_default();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
}



void app_main()
{
    espnow_storage_init();

    ESP_ERROR_CHECK(audio_play_init());

    app_wifi_init();

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_init(&espnow_config);
    
    master_evt_queue = xQueueCreate(8, sizeof(master_evt_msg_t));

    //ESP_ERROR_CHECK(audio_play_set_io_mask(0xFF));


    // vTaskDelay(pdMS_TO_TICKS(2000));

    
    // esp_err_t ret = audio_play_trigger_once(0, 100);
    // if (ret == ESP_OK) {
    //     ESP_LOGI(TAG, "trigger music IO%u ok", 0);
    // } else {
    //     ESP_LOGE(TAG, "trigger music IO%u failed: %s", 0, esp_err_to_name(ret));
    // }

    //     vTaskDelay(pdMS_TO_TICKS(5000));

    // espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_DATA, true, master_receive_handle);
    // //初始化完成，开始配对

    // xTaskCreate(ms_pairing_task,
    //         "ms_pairing",
    //         4096,
    //         NULL,
    //         4,
    //         NULL);

    // xTaskCreate(master_action_verify_task,
    //     "action_verify",
    //     4096,
    //     NULL,
    //     4,
    //     NULL);

    xTaskCreate(audio_play_event_task,
        "audio_evt",
        2048,
        NULL,
        3,
        NULL);

    xTaskCreate(busy_monitor_log_task,
        "busy_log",
        2048,
        NULL,
        3,
        NULL);

    xTaskCreate(audio_play_test_task,
        "audio_test",
        2048,
        NULL,
        3,
        NULL);



    
}
