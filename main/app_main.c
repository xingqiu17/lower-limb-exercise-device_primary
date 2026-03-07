/* Get Start Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)

#endif

#include "espnow.h"
#include "espnow_storage.h"
#include "espnow_utils.h"


#include "espnow_sr.h"
#include "state_machine.h"

static const char *TAG = "app_main";

master_state_t m_state = MASTER_IDLE;
slave_state_t s_state[2] = {SLAVE_IDLE,SLAVE_IDLE};




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

    app_wifi_init();

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_init(&espnow_config);
    
    master_evt_queue = xQueueCreate(8, sizeof(master_evt_msg_t));


    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_DATA, true, master_receive_handle);
    //初始化完成，开始配对

    xTaskCreate(ms_pairing_task,
            "ms_pairing",
            4096,
            NULL,
            4,
            NULL);

    xTaskCreate(master_action_verify_task,
        "action_verify",
        4096,
        NULL,
        4,
        NULL);



    
}
