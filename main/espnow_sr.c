#include "espnow_sr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "state_machine.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAGR = "receive_handle";

static const char *TAG  = "esp_now";

uint32_t seq = 0;







slave_mac_info_t g_slave_macs[MAX_SLAVES] = {0};
uint8_t g_slave_count = 0;

QueueHandle_t master_evt_queue = NULL;


static int find_slave_mac(const uint8_t *mac)
{
    for (int i = 0; i < MAX_SLAVES; i++) {
        if (g_slave_macs[i].valid &&
            memcmp(g_slave_macs[i].mac, mac, 6) == 0) {
            return i;   // 找到
        }
    }
    return -1;          // 没找到
}


esp_err_t nvs_save_slave_mac(const uint8_t *mac)
{
    if (!mac) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 1. 查重 */
    if (find_slave_mac(mac) >= 0) {
        ESP_LOGI(TAG, "Slave MAC already exists, skip saving");
        return ESP_OK;
    }

    /* 2. 是否已满 */
    if (g_slave_count >= MAX_SLAVES) {
        ESP_LOGW(TAG, "Slave MAC list full");
        return ESP_ERR_NO_MEM;
    }

    /* 3. 保存到 RAM 全局变量 */
    for (int i = 0; i < MAX_SLAVES; i++) {
        if (!g_slave_macs[i].valid) {
            memcpy(g_slave_macs[i].mac, mac, 6);
            g_slave_macs[i].valid = true;
            g_slave_count++;
            break;
        }
    }

    /* 4. 保存到 NVS */
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("pairing", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(nvs, "slave_macs",
                       g_slave_macs,
                       sizeof(g_slave_macs));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob failed: %s", esp_err_to_name(err));
        nvs_close(nvs);
        return err;
    }

    err = nvs_commit(nvs);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Slave MAC saved (%d/%d)",
                 g_slave_count, MAX_SLAVES);
    } else {
        ESP_LOGE(TAG, "nvs_commit failed: %s",
                 esp_err_to_name(err));
    }

    nvs_close(nvs);
    return err;
}


static bool mark_slave_ready(const uint8_t *mac)
{
    int idx = find_slave_mac(mac);

    if (idx < 0) {
        ESP_LOGW(TAG, "READY confirm from unknown slave");
        return false;
    }

    if (g_slave_macs[idx].ready) {
        ESP_LOGI(TAG, "Slave already READY");
        return false;
    }

    g_slave_macs[idx].ready = true;

    ESP_LOGI(TAG,
        "Slave READY: %02X:%02X:%02X:%02X:%02X:%02X",
        mac[0], mac[1], mac[2],
        mac[3], mac[4], mac[5]);

    return true;
}


static bool all_slaves_ready(void)
{
    int ready_cnt = 0;

    for (int i = 0; i < MAX_SLAVES; i++) {
        if (g_slave_macs[i].valid &&
            g_slave_macs[i].ready) {
            ready_cnt++;
        }
    }

    return (ready_cnt >= MAX_SLAVES);
}



void ms_pairing_task(void *arg)
{
    master_state_t state = MASTER_IDLE;
    master_evt_msg_t evt;

    espnow_frame_head_t frame_head = {};
    frame_head.retransmit_count = 5;
    frame_head.broadcast        = true;

    TickType_t last_send_tick = 0;

    /* 启动配对 */
    state = MASTER_PAIRING;
    ESP_LOGI(TAG, "MASTER: start pairing");

    while (1) {

        switch (state) {

        case MASTER_PAIRING: {
            /* 1. 周期性广播配对请求 */
            if (xTaskGetTickCount() - last_send_tick >
                pdMS_TO_TICKS(1000)) {

                esp_now_data pair_request = {
                    .type = CONNECTION_REQUEST,
                    .seq  = seq++,
                    .data = 0
                };

                espnow_send(ESPNOW_DATA_TYPE_DATA,
                            ESPNOW_ADDR_BROADCAST,
                            &pair_request,
                            sizeof(pair_request),
                            &frame_head,
                            portMAX_DELAY);

                last_send_tick = xTaskGetTickCount();
                ESP_LOGI(TAG, "MASTER: broadcast pairing request");
            }

            /* 2. 处理从设备响应 */
            if (xQueueReceive(master_evt_queue, &evt,
                              pdMS_TO_TICKS(100))) {
                
                switch(evt.event){

                    case EVT_SLAVE_CONFIRM: {

                        nvs_save_slave_mac(evt.slave_mac);
                        espnow_add_peer(evt.slave_mac, NULL);

                        /* 3. 单播确认给该从设备 */
                        esp_now_data confirm = {
                            .type = CONNECTION_MASTER_CONFIRM,
                            .seq  = seq++,
                            .data = 0
                        };

                        frame_head.broadcast = false;

                        espnow_send(ESPNOW_DATA_TYPE_DATA,
                                    evt.slave_mac,
                                    &confirm,
                                    sizeof(confirm),
                                    &frame_head,
                                    portMAX_DELAY);

                        frame_head.broadcast = true;

                        
                    }break;

                    case EVT_SLAVE_STATUS: {

                        /* 1. 只接受 READY 确认 */
                        if (evt.data != 666) {
                            ESP_LOGI(TAG, "Ignore slave status: %lu", evt.data);
                            break;
                        }

                        /* 2. 保存 MAC（自动查重） */
                        nvs_save_slave_mac(evt.slave_mac);

                        /* 3. 标记 READY（防止重复） */
                        if (!mark_slave_ready(evt.slave_mac)) {
                            break;
                        }

                        /* 4. 是否全部 READY */
                        if (all_slaves_ready()) {
                            ESP_LOGI(TAG, "MASTER: all slaves READY");
                            state = MASTER_READY;
                        }
                    }break;

                    default :{

                    }break;
                }
                
            }
            


            break;
        }

        case MASTER_READY:
            ESP_LOGI(TAG, "MASTER: pairing done, enter READY");
            vTaskDelete(NULL);
            break;

        default:
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        }
    }
}











esp_err_t master_receive_handle(uint8_t *src_addr,
                                       void *data,
                                       size_t size,
                                       wifi_pkt_rx_ctrl_t *rx_ctrl)
{
  //static uint32_t count = 0;
  const esp_now_data *pkt = (const esp_now_data *)data;

  switch(pkt->type){

    //接收从设备配对确认
    case CONNECTION_SLAVE_CONFIRM:{

        ESP_LOGI(TAGR,"Recevice Slave Confirm");
        master_evt_msg_t msg = {
          .event = EVT_SLAVE_CONFIRM,
          .data  = pkt->data
        };
        memcpy(msg.slave_mac, src_addr, 6);
        xQueueSend(master_evt_queue, &msg, 0);


    }break;


    //接收从设备的状态确认
    case STATUS_CONFIRM:{

        ESP_LOGI(TAGR,"Recevice Slave Status");
        master_evt_msg_t msg = {
          .event = EVT_SLAVE_STATUS,
          .data  = pkt->data
        };
        memcpy(msg.slave_mac, src_addr, 6);
        xQueueSend(master_evt_queue, &msg, 0);
      

    }break;


    //接收从设备如计步等运动数据（暂未详细定义）
    case EXERCISE_DATA:{

        ESP_LOGI(TAGR,"Recevice Slave Exercise Data");
        master_evt_msg_t msg = {
          .event = EVT_SLAVE_EXERCISE_DATA,
          .data  = pkt->data
        };
        memcpy(msg.slave_mac, src_addr, 6);
        xQueueSend(master_evt_queue, &msg, 0);
      

    }break;




    default:{
      ESP_LOGW(TAG,
             "Unknown packet type: %d, len=%u from %02X:%02X:%02X:%02X:%02X:%02X",
             pkt->type,
             (unsigned)size,
             src_addr[0], src_addr[1], src_addr[2],
             src_addr[3], src_addr[4], src_addr[5]);
    }break;

  }


  

  return ESP_OK;
}
