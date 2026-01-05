#include "espnow_sr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "state_machine.h"

static const char *TAG = "esp-now";

uint32_t seq = 0;

void ms_pairing_task(void *arg)
{
    master_state_t state = MASTER_IDLE;
    master_event_t event;

    // 启动配对
    event = EVT_PAIR_START;
    state = master_state_machine(state, event);

    espnow_frame_head_t frame_head = {};
    frame_head.retransmit_count = 5;
    frame_head.broadcast        = true;

    TickType_t last_send_tick = 0;

    while (1) {
        switch (state) {

            case MASTER_PAIRING:{


                esp_now_data pair_request = {CONNECTION_REQUEST,seq++,0};
                espnow_send(ESPNOW_DATA_TYPE_DATA, ESPNOW_ADDR_BROADCAST, &pair_request, sizeof(pair_request), &frame_head, portMAX_DELAY);

                last_send_tick = xTaskGetTickCount();


                }break;

            case MASTER_WAIT_ALL_CONFIRM:{
                esp_now_data pair_request = {CONNECTION_REQUEST,seq++,0};
                espnow_send(ESPNOW_DATA_TYPE_DATA, ESPNOW_ADDR_BROADCAST, &pair_request, sizeof(pair_request), &frame_head, portMAX_DELAY);

                // 等事件（来自回调 or 超时）
                // if (xQueueReceive(pair_evt_queue, &event,
                //                    pdMS_TO_TICKS(100))) {
                //     state = master_state_machine(state, event);
                // } else {
                //     // 超时
                //     if (xTaskGetTickCount() - last_send_tick >
                //         pdMS_TO_TICKS(2000)) {
                //         state = master_state_machine(state,
                //                                      EVT_PAIR_TIMEOUT);
                //     }
                // }
                }break;

            case MASTER_READY:{
                ESP_LOGI(TAG, "MASTER: pairing done");
                vTaskDelete(NULL);
            }break;

            default:{
                vTaskDelay(pdMS_TO_TICKS(100));
            }break;
        }
    }
}






esp_err_t app_uart_write_handle(uint8_t *src_addr, void *data,
                                       size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    static uint32_t count = 0;

    ESP_LOGI(TAG, "espnow_recv, <%" PRIu32 "> [" MACSTR "][%d][%d][%u]: %.*s",
             count++, MAC2STR(src_addr), rx_ctrl->channel, rx_ctrl->rssi, size, size, (char *)data);

    return ESP_OK;
}