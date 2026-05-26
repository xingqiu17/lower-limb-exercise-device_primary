#include "espnow_sr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "state_machine.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "neopixel_ctrl.h"

static const char *TAGR = "receive_handle";

static const char *TAG  = "esp_now";

#define KEEP_ALIVE_TIMEOUT_MS  6000
#define KEEP_ALIVE_CHECK_MS     500

uint32_t seq = 0;

uint32_t g_current_action_mode = 0;
uint32_t g_current_action_count = 0;
static bool g_pairing_event_accepting = true;
static volatile bool g_shutdown_in_progress = false;
static TaskHandle_t s_test_task_handle = NULL;
static volatile bool s_test_task_enabled = false;
static volatile uint32_t s_test_reply_count = 0;

static TickType_t g_slave_last_keepalive_tick[MAX_SLAVES] = {0};
static bool g_slave_keepalive_online[MAX_SLAVES] = {false};







slave_mac_info_t g_slave_macs[MAX_SLAVES] = {0};
uint8_t g_slave_count = 0;

QueueHandle_t master_evt_queue = NULL;

void master_set_shutdown_in_progress(bool in_progress)
{
    g_shutdown_in_progress = in_progress;
    ESP_LOGI(TAG, "shutdown_in_progress=%d", (int)g_shutdown_in_progress);
}

bool master_is_shutdown_in_progress(void)
{
    return g_shutdown_in_progress;
}

void master_set_current_action_mode(uint32_t action_mode)
{
    if (action_mode > 4) {
        ESP_LOGW(TAG, "Invalid action mode: %lu", (unsigned long)action_mode);
        return;
    }

    g_current_action_mode = action_mode;
    g_current_action_count = 0;

    esp_err_t led_ret = neopixel_ctrl_set_gpio1_progress_red(0);
    if (led_ret != ESP_OK) {
        ESP_LOGW(TAG, "Reset GPIO1 progress LED failed: %s", esp_err_to_name(led_ret));
    }

    ESP_LOGI(TAG, "Set current action mode=%lu, count reset", (unsigned long)g_current_action_mode);
}


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

#if ESPNOW_SR_TEST_TASK_ENABLE
static esp_err_t master_send_test_to_ready_slaves(void)
{
    espnow_frame_head_t frame_head = {};
    frame_head.retransmit_count = 1;
    frame_head.broadcast = false;

    esp_now_data cmd = {
        .type = TEST,
        .seq = seq++,
        .data = 0,
    };

    uint8_t sent_count = 0;
    esp_err_t last_err = ESP_OK;

    for (int i = 0; i < MAX_SLAVES; i++) {
        if (!g_slave_macs[i].valid || !g_slave_macs[i].ready) {
            continue;
        }

        esp_err_t err = espnow_send(ESPNOW_DATA_TYPE_DATA,
                                    g_slave_macs[i].mac,
                                    &cmd,
                                    sizeof(cmd),
                                    &frame_head,
                                    portMAX_DELAY);
        if (err == ESP_OK) {
            sent_count++;
        } else {
            last_err = err;
            ESP_LOGW(TAG,
                     "TEST send failed to %02X:%02X:%02X:%02X:%02X:%02X, err=%s",
                     g_slave_macs[i].mac[0], g_slave_macs[i].mac[1], g_slave_macs[i].mac[2],
                     g_slave_macs[i].mac[3], g_slave_macs[i].mac[4], g_slave_macs[i].mac[5],
                     esp_err_to_name(err));
        }
    }

    if (sent_count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    return last_err;
}

static void master_test_task(void *arg)
{
    (void)arg;

    uint32_t sent = 0;
    TickType_t last_wait_log_tick = 0;

    s_test_reply_count = 0;
    ESP_LOGI(TAG,
             "TEST task start: send %u packets, interval=%u ms",
             (unsigned)ESPNOW_SR_TEST_PACKET_COUNT,
             (unsigned)ESPNOW_SR_TEST_PACKET_INTERVAL_MS);

    while (s_test_task_enabled && sent < ESPNOW_SR_TEST_PACKET_COUNT) {
        if (g_shutdown_in_progress) {
            vTaskDelay(pdMS_TO_TICKS(ESPNOW_SR_TEST_PACKET_INTERVAL_MS));
            continue;
        }

        esp_err_t err = master_send_test_to_ready_slaves();
        if (err == ESP_OK) {
            sent++;
            ESP_LOGI(TAG,
                     "TEST sent %lu/%u, reply_count=%lu",
                     (unsigned long)sent,
                     (unsigned)ESPNOW_SR_TEST_PACKET_COUNT,
                     (unsigned long)s_test_reply_count);
        } else if (err == ESP_ERR_NOT_FOUND) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_wait_log_tick) >= pdMS_TO_TICKS(1000)) {
                ESP_LOGI(TAG, "TEST task waiting for READY slave");
                last_wait_log_tick = now;
            }
        } else {
            ESP_LOGW(TAG, "TEST send error: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(ESPNOW_SR_TEST_PACKET_INTERVAL_MS));
    }

    ESP_LOGI(TAG,
             "TEST task done: sent=%lu, reply_count=%lu",
             (unsigned long)sent,
             (unsigned long)s_test_reply_count);
    s_test_task_enabled = false;
    s_test_task_handle = NULL;
    vTaskDelete(NULL);
}
#endif

esp_err_t master_start_test_task(void)
{
#if ESPNOW_SR_TEST_TASK_ENABLE
    if (s_test_task_handle != NULL) {
        return ESP_OK;
    }

    s_test_task_enabled = true;
    s_test_reply_count = 0;

    BaseType_t task_ok = xTaskCreate(master_test_task,
                                     "ms_test",
                                     3072,
                                     NULL,
                                     3,
                                     &s_test_task_handle);
    if (task_ok != pdPASS) {
        s_test_task_enabled = false;
        s_test_task_handle = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
#else
    ESP_LOGI(TAG, "TEST task disabled by ESPNOW_SR_TEST_TASK_ENABLE=0");
    return ESP_OK;
#endif
}

void master_stop_test_task(void)
{
#if ESPNOW_SR_TEST_TASK_ENABLE
    s_test_task_enabled = false;

    if (s_test_task_handle != NULL) {
        TaskHandle_t task = s_test_task_handle;
        s_test_task_handle = NULL;
        vTaskDelete(task);
        ESP_LOGI(TAG, "TEST task stopped");
    }
#endif
}

uint32_t master_get_test_reply_count(void)
{
    return s_test_reply_count;
}

void master_reset_test_reply_count(void)
{
    s_test_reply_count = 0;
}

static void update_slave_keepalive_tick(const uint8_t *mac)
{
    if (mac == NULL) {
        return;
    }

    int idx = find_slave_mac(mac);

    if (idx < 0) {
        (void)nvs_save_slave_mac(mac);
        idx = find_slave_mac(mac);
        if (idx < 0) {
            ESP_LOGW(TAG, "KEEP_ALIVE from unknown slave and no free slot");
            return;
        }
    }

    g_slave_last_keepalive_tick[idx] = xTaskGetTickCount();

    if (!g_slave_keepalive_online[idx]) {
        g_slave_keepalive_online[idx] = true;
        ESP_LOGI(TAG,
                 "Slave online by KEEP_ALIVE: %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    if (!g_slave_macs[idx].ready) {
        g_slave_macs[idx].ready = true;
        ESP_LOGI(TAG,
                 "Slave READY restored by KEEP_ALIVE: %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
}

void master_keepalive_monitor_task(void *arg)
{
    (void)arg;

    while (1) {
        if (g_shutdown_in_progress) {
            vTaskDelay(pdMS_TO_TICKS(KEEP_ALIVE_CHECK_MS));
            continue;
        }

        TickType_t now = xTaskGetTickCount();

        for (int i = 0; i < MAX_SLAVES; i++) {
            if (!g_slave_macs[i].valid || !g_slave_keepalive_online[i]) {
                continue;
            }

            TickType_t elapsed = now - g_slave_last_keepalive_tick[i];
            if (elapsed >= pdMS_TO_TICKS(KEEP_ALIVE_TIMEOUT_MS)) {
                g_slave_keepalive_online[i] = false;
                g_slave_macs[i].ready = false;

                ESP_LOGW(TAG,
                         "Slave keepalive timeout (> %d ms): %02X:%02X:%02X:%02X:%02X:%02X, set NOT READY",
                         KEEP_ALIVE_TIMEOUT_MS,
                         g_slave_macs[i].mac[0], g_slave_macs[i].mac[1], g_slave_macs[i].mac[2],
                         g_slave_macs[i].mac[3], g_slave_macs[i].mac[4], g_slave_macs[i].mac[5]);

                app_handle_slave_link_lost();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(KEEP_ALIVE_CHECK_MS));
    }
}


esp_err_t master_send_action_to_ready_slaves(uint32_t action_code)
{
    espnow_frame_head_t frame_head = {};
    frame_head.retransmit_count = 5;
    frame_head.broadcast = false;

    esp_now_data cmd = {
        .type = STATUS_CHANGE,
        .seq = seq++,
        .data = action_code,
    };

    uint8_t sent_count = 0;
    esp_err_t last_err = ESP_OK;

    for (int i = 0; i < MAX_SLAVES; i++) {
        if (!g_slave_macs[i].valid || !g_slave_macs[i].ready) {
            continue;
        }

        esp_err_t err = espnow_send(ESPNOW_DATA_TYPE_DATA,
                                    g_slave_macs[i].mac,
                                    &cmd,
                                    sizeof(cmd),
                                    &frame_head,
                                    portMAX_DELAY);

        if (err == ESP_OK) {
            sent_count++;
            ESP_LOGI(TAG,
                     "MASTER: send action=%lu to %02X:%02X:%02X:%02X:%02X:%02X",
                     (unsigned long)action_code,
                     g_slave_macs[i].mac[0], g_slave_macs[i].mac[1], g_slave_macs[i].mac[2],
                     g_slave_macs[i].mac[3], g_slave_macs[i].mac[4], g_slave_macs[i].mac[5]);
        } else {
            last_err = err;
            ESP_LOGE(TAG,
                     "MASTER: failed action send to %02X:%02X:%02X:%02X:%02X:%02X, err=%s",
                     g_slave_macs[i].mac[0], g_slave_macs[i].mac[1], g_slave_macs[i].mac[2],
                     g_slave_macs[i].mac[3], g_slave_macs[i].mac[4], g_slave_macs[i].mac[5],
                     esp_err_to_name(err));
        }
    }

    if (sent_count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    return last_err;
}

esp_err_t master_send_power_manage_to_ready_slaves(uint32_t power_data)
{
    if (power_data > 1) {
        return ESP_ERR_INVALID_ARG;
    }

    espnow_frame_head_t frame_head = {};
    frame_head.retransmit_count = 5;
    frame_head.broadcast = false;

    esp_now_data cmd = {
        .type = POWER_MANAGE,
        .seq = seq++,
        .data = power_data,
    };

    uint8_t sent_count = 0;
    esp_err_t last_err = ESP_OK;

    for (int i = 0; i < MAX_SLAVES; i++) {
        if (!g_slave_macs[i].valid || !g_slave_macs[i].ready) {
            continue;
        }

        esp_err_t err = espnow_send(ESPNOW_DATA_TYPE_DATA,
                                    g_slave_macs[i].mac,
                                    &cmd,
                                    sizeof(cmd),
                                    &frame_head,
                                    portMAX_DELAY);

        if (err == ESP_OK) {
            sent_count++;
            ESP_LOGI(TAG,
                     "MASTER: send POWER_MANAGE=%lu to %02X:%02X:%02X:%02X:%02X:%02X",
                     (unsigned long)power_data,
                     g_slave_macs[i].mac[0], g_slave_macs[i].mac[1], g_slave_macs[i].mac[2],
                     g_slave_macs[i].mac[3], g_slave_macs[i].mac[4], g_slave_macs[i].mac[5]);
        } else {
            last_err = err;
            ESP_LOGE(TAG,
                     "MASTER: failed POWER_MANAGE send to %02X:%02X:%02X:%02X:%02X:%02X, err=%s",
                     g_slave_macs[i].mac[0], g_slave_macs[i].mac[1], g_slave_macs[i].mac[2],
                     g_slave_macs[i].mac[3], g_slave_macs[i].mac[4], g_slave_macs[i].mac[5],
                     esp_err_to_name(err));
        }
    }

    if (sent_count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    return last_err;
}

bool master_wait_slave_power_ack(uint32_t timeout_ms)
{
    if (master_evt_queue == NULL) {
        return false;
    }

    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    master_evt_msg_t evt;

    while (1) {
        TickType_t now = xTaskGetTickCount();
        if ((int32_t)(deadline - now) <= 0) {
            break;
        }

        TickType_t remain = deadline - now;
        TickType_t wait_ticks = remain > pdMS_TO_TICKS(50) ? pdMS_TO_TICKS(50) : remain;

        if (xQueueReceive(master_evt_queue, &evt, wait_ticks) != pdTRUE) {
            continue;
        }

        if ((evt.event == EVT_SLAVE_POWER_ACK) && (evt.data == 2)) {
            ESP_LOGI(TAG,
                     "MASTER: recv slave power ack from %02X:%02X:%02X:%02X:%02X:%02X",
                     evt.slave_mac[0], evt.slave_mac[1], evt.slave_mac[2],
                     evt.slave_mac[3], evt.slave_mac[4], evt.slave_mac[5]);
            return true;
        }
    }

    return false;
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
    g_pairing_event_accepting = true;
    state = MASTER_PAIRING;
    ESP_LOGI(TAG, "MASTER: start pairing");

    while (1) {

        switch (state) {

        case MASTER_PAIRING: {
            /* 1. 周期性广播配对请求 */
            if (xTaskGetTickCount() - last_send_tick >
                pdMS_TO_TICKS(1000)) {

                // 先广播开机请求，确保从机被主机拉起（data=1）。
                esp_now_data power_on_request = {
                    .type = POWER_MANAGE,
                    .seq  = seq++,
                    .data = 1
                };

                espnow_send(ESPNOW_DATA_TYPE_DATA,
                            ESPNOW_ADDR_BROADCAST,
                            &power_on_request,
                            sizeof(power_on_request),
                            &frame_head,
                            portMAX_DELAY);

                ESP_LOGI(TAG, "MASTER: broadcast power on request");

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
            g_pairing_event_accepting = false;
            if (master_evt_queue != NULL) {
                xQueueReset(master_evt_queue);
            }
            app_handle_slave_link_restored();
            app_mark_pairing_task_done();
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
    if (!data || size < sizeof(esp_now_data)) {
        ESP_LOGW(TAGR, "Drop invalid packet: size=%u", (unsigned)size);
        return ESP_ERR_INVALID_SIZE;
    }

    const esp_now_data *pkt = (const esp_now_data *)data;

    ESP_LOGI(TAGR,
                     "recv type=%d seq=%lu src=%02X:%02X:%02X:%02X:%02X:%02X ch=%d len=%u",
                     pkt->type,
                     (unsigned long)pkt->seq,
                     src_addr[0], src_addr[1], src_addr[2],
                     src_addr[3], src_addr[4], src_addr[5],
                     rx_ctrl ? rx_ctrl->channel : -1,
                     (unsigned)size);

    if (g_shutdown_in_progress && pkt->type != POWER_MANAGE) {
            ESP_LOGI(TAGR, "Ignore packet type=%d during shutdown", pkt->type);
            return ESP_OK;
    }

    switch(pkt->type){

    //接收从设备配对确认
    case CONNECTION_SLAVE_CONFIRM:{

        ESP_LOGI(TAGR,"Recevice Slave Confirm");
                if (g_pairing_event_accepting && master_evt_queue != NULL) {
                        master_evt_msg_t msg = {
                            .event = EVT_SLAVE_CONFIRM,
                            .data  = pkt->data
                        };
                        memcpy(msg.slave_mac, src_addr, 6);
                        if (xQueueSend(master_evt_queue, &msg, 0) != pdTRUE) {
                                ESP_LOGW(TAGR, "Queue full, drop EVT_SLAVE_CONFIRM");
                        }
                }


    }break;


    //接收从设备的状态确认
    case STATUS_CONFIRM:{

        ESP_LOGI(TAGR,"Recevice Slave Status %lu", (unsigned long)pkt->data);
                if (g_pairing_event_accepting && master_evt_queue != NULL) {
                        master_evt_msg_t msg = {
                            .event = EVT_SLAVE_STATUS,
                            .data  = pkt->data
                        };
                        memcpy(msg.slave_mac, src_addr, 6);
                        if (xQueueSend(master_evt_queue, &msg, 0) != pdTRUE) {
                                ESP_LOGW(TAGR, "Queue full, drop EVT_SLAVE_STATUS");
                        }
        }
      

    }break;


    //接收从设备如计步等运动数据（暂未详细定义）
    case EXERCISE_DATA:{

        uint32_t mode = pkt->data;

        if (mode == g_current_action_mode && mode >= 1 && mode <= 4) {
            g_current_action_count++;
            uint16_t progress_led_count = (uint16_t)(g_current_action_count / 2U);
            if (progress_led_count > 10U) {
                progress_led_count = 10U;
            }

            esp_err_t led_ret = neopixel_ctrl_set_gpio1_progress_red(progress_led_count);
            if (led_ret != ESP_OK) {
                ESP_LOGW(TAGR,
                         "Update GPIO1 progress LED failed, count=%lu, err=%s",
                         (unsigned long)g_current_action_count,
                         esp_err_to_name(led_ret));
            }

            ESP_LOGI(TAGR,
                     "Exercise matched: mode=%lu, count=%lu",
                     (unsigned long)mode,
                     (unsigned long)g_current_action_count);
        }

        ESP_LOGI(TAGR,"Recevice Slave Exercise Data");
      

    }break;

    case KEEP_ALIVE: {
        if (g_shutdown_in_progress) {
            ESP_LOGI(TAGR, "Ignore KEEP_ALIVE during shutdown");
            break;
        }

        if (pkt->data != 0) {
            ESP_LOGI(TAGR, "Ignore KEEP_ALIVE ack from slave, data=%lu", (unsigned long)pkt->data);
            break;
        }

        update_slave_keepalive_tick(src_addr);

        esp_now_data ack = {
            .type = KEEP_ALIVE,
            .seq  = seq++,
            .data = 1,
        };

        espnow_frame_head_t frame_head = {};
        frame_head.retransmit_count = 3;
        frame_head.broadcast = false;

        esp_err_t ack_ret = espnow_send(ESPNOW_DATA_TYPE_DATA,
                                        src_addr,
                                        &ack,
                                        sizeof(ack),
                                        &frame_head,
                                        portMAX_DELAY);
        if (ack_ret != ESP_OK) {
            ESP_LOGW(TAGR,
                     "KEEP_ALIVE ack failed to %02X:%02X:%02X:%02X:%02X:%02X, err=%s",
                     src_addr[0], src_addr[1], src_addr[2],
                     src_addr[3], src_addr[4], src_addr[5],
                     esp_err_to_name(ack_ret));
        }

    }break;

    case POWER_MANAGE: {
        // 从设备电源管理确认: data=2
        if ((pkt->data == 2) && (master_evt_queue != NULL)) {
            master_evt_msg_t msg = {
                .event = EVT_SLAVE_POWER_ACK,
                .data  = pkt->data
            };
            memcpy(msg.slave_mac, src_addr, 6);
            if (xQueueSend(master_evt_queue, &msg, 0) != pdTRUE) {
                ESP_LOGW(TAGR, "Queue full, drop EVT_SLAVE_POWER_ACK");
            }
        } else {
            ESP_LOGI(TAGR, "Ignore POWER_MANAGE packet data=%lu", (unsigned long)pkt->data);
        }
    }break;

    case TEST: {
        if (pkt->data == 1) {
            uint32_t reply_count = ++s_test_reply_count;
            ESP_LOGI(TAGR,
                     "TEST reply count=%lu from %02X:%02X:%02X:%02X:%02X:%02X",
                     (unsigned long)reply_count,
                     src_addr[0], src_addr[1], src_addr[2],
                     src_addr[3], src_addr[4], src_addr[5]);
        } else {
            ESP_LOGI(TAGR, "Ignore TEST packet data=%lu", (unsigned long)pkt->data);
        }
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
