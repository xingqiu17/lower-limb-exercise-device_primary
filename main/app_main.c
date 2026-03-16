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

#define APP_TRACK_COUNT            4
#define APP_TRACK_PLAYED_MASK_ALL  ((1U << APP_TRACK_COUNT) - 1U)
#define APP_TRIGGER_LOW_TIME_MS    100

master_state_t m_state = MASTER_IDLE;
slave_state_t s_state[2] = {SLAVE_IDLE,SLAVE_IDLE};



static void audio_play_event_task(void *arg)
{
    QueueHandle_t evt_queue = audio_play_get_event_queue();
    audio_play_event_t evt;
    bool auto_play_active = false;
    uint8_t played_track_mask = 0;
    int8_t current_track = -1;
    int8_t first_pressed_button = -1;

    if (evt_queue == NULL) {
        ESP_LOGE(TAG, "audio_play event queue is null");
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        if (xQueueReceive(evt_queue, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (evt.type == AUDIO_PLAY_EVENT_BUTTON_PRESSED) {
            uint8_t logical_io = evt.logical_io;

            if (logical_io >= APP_TRACK_COUNT) {
                ESP_LOGW(TAG, "ignore invalid button event: button=%u logical_io=%u", evt.button_index, logical_io);
                continue;
            }

            if (auto_play_active) {
                ESP_LOGI(TAG, "button%u ignored, music sequence is running", evt.button_index);
                continue;
            }

            esp_err_t trigger_ret = audio_play_trigger_once(logical_io, APP_TRIGGER_LOW_TIME_MS);
            if (trigger_ret != ESP_OK) {
                ESP_LOGE(TAG, "button%u trigger IO%u failed: %s", evt.button_index, logical_io, esp_err_to_name(trigger_ret));
                continue;
            }

            auto_play_active = true;
            played_track_mask = (1U << logical_io);
            current_track = (int8_t)logical_io;
            first_pressed_button = (int8_t)evt.button_index;

            ESP_LOGI(TAG, "button%u pressed, trigger IO%u, start auto-play sequence", evt.button_index, logical_io);
            continue;
        }

        if (evt.type == AUDIO_PLAY_EVENT_BUSY_RISING) {
            ESP_LOGI(TAG, "recv BUSY rising event, tick=%lu", (unsigned long)evt.tick);

            if (!auto_play_active || current_track < 0) {
                continue;
            }

            if (played_track_mask == APP_TRACK_PLAYED_MASK_ALL) {
                ESP_LOGI(TAG, "auto-play done after first button%u, clear recorded button and stop", first_pressed_button);
                auto_play_active = false;
                played_track_mask = 0;
                current_track = -1;
                first_pressed_button = -1;
                continue;
            }

            int8_t next_track = -1;
            for (uint8_t offset = 1; offset <= APP_TRACK_COUNT; ++offset) {
                uint8_t candidate = ((uint8_t)current_track + offset) % APP_TRACK_COUNT;
                if ((played_track_mask & (1U << candidate)) == 0) {
                    next_track = (int8_t)candidate;
                    break;
                }
            }

            if (next_track < 0) {
                ESP_LOGW(TAG, "no next track found, clear auto-play state");
                auto_play_active = false;
                played_track_mask = 0;
                current_track = -1;
                first_pressed_button = -1;
                continue;
            }

            esp_err_t trigger_ret = audio_play_trigger_once((uint8_t)next_track, APP_TRIGGER_LOW_TIME_MS);
            if (trigger_ret != ESP_OK) {
                ESP_LOGE(TAG, "auto-play trigger IO%u failed: %s", (uint8_t)next_track, esp_err_to_name(trigger_ret));
                continue;
            }

            played_track_mask |= (1U << next_track);
            current_track = next_track;
            ESP_LOGI(TAG, "auto-play next IO%u, played_mask=0x%02X", (uint8_t)next_track, played_track_mask);
        }
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




    
}
