#include "state_machine.h"
#include "esp_log.h"

static const char *TAG = "state_machine";

/*----------------------主设备状态机----------------------*/
master_state_t master_state_machine(master_state_t cur_state, master_event_t event)
{
    master_state_t next_state = cur_state;

    switch (cur_state) {
        case MASTER_IDLE:
            if (event == EVT_PAIR_START) {
                next_state = MASTER_PAIRING;
                ESP_LOGI(TAG, "MASTER: start pairing -> MASTER_PAIRING");
            }
            break;

        case MASTER_PAIRING:
            if (event == EVT_SLAVE_RESP) {
                next_state = MASTER_WAIT_ALL_CONFIRM;
                ESP_LOGI(TAG, "MASTER: received slave response -> MASTER_WAIT_ALL_CONFIRM");
            } else if (event == EVT_PAIR_TIMEOUT) {
                ESP_LOGW(TAG, "MASTER: pair timeout, retrying -> MASTER_PAIRING");
                next_state = MASTER_PAIRING;  // 超时重发请求
            }
            break;

        case MASTER_WAIT_ALL_CONFIRM:
            if (event == EVT_MASTER_RESP) {
                next_state = MASTER_READY;
                ESP_LOGI(TAG, "MASTER: handshake complete -> MASTER_READY");
            } else if (event == EVT_PAIR_TIMEOUT) {
                ESP_LOGW(TAG, "MASTER: waiting all slave responses timeout -> MASTER_PAIRING");
                next_state = MASTER_PAIRING;  // 超时重新开始配对
            }
            break;

        case MASTER_READY:
            if (event == EVT_START_WORK) {
                next_state = MASTER_RUNNING;
                ESP_LOGI(TAG, "MASTER: start work -> MASTER_RUNNING");
            }
            break;

        case MASTER_RUNNING:
            if (event == EVT_STOP_WORK) {
                next_state = MASTER_IDLE;
                ESP_LOGI(TAG, "MASTER: stop work -> MASTER_IDLE");
            }
            break;

        default:
            if (event == EVT_ERROR) {
                next_state = MASTER_ERROR;
                ESP_LOGE(TAG, "MASTER: error occurred -> MASTER_ERROR");
            }
            break;
    }

    return next_state;
}




slave_state_t slave_state_machine(slave_state_t cur_state, slave_event_t event)
{
    slave_state_t next_state = cur_state;

    switch (cur_state) {
        case SLAVE_IDLE:
            if (event == EVT_MASTER_REQ) {
                next_state = SLAVE_WAIT_MAIN_CONFIRM;
                ESP_LOGI(TAG, "SLAVE: received master request -> SLAVE_WAIT_MAIN_CONFIRM");
            }
            break;

        case SLAVE_WAIT_MAIN_CONFIRM:
            if (event == EVT_MASTER_CONFIRM) {
                next_state = SLAVE_READY;
                ESP_LOGI(TAG, "SLAVE: received master final confirm -> SLAVE_READY");
            }
            break;

        case SLAVE_READY:
            if (event == EVT_START_WORK) {
                next_state = SLAVE_RUNNING;
                ESP_LOGI(TAG, "SLAVE: start work -> SLAVE_RUNNING");
            }
            break;

        case SLAVE_RUNNING:
            if (event == EVT_STOP_WORK) {
                next_state = SLAVE_IDLE;
                ESP_LOGI(TAG, "SLAVE: stop work -> SLAVE_IDLE");
            }
            break;

        default:
            if (event == EVT_ERROR) {
                next_state = SLAVE_IDLE;
                ESP_LOGE(TAG, "SLAVE: error occurred -> SLAVE_IDLE");
            }
            break;
    }

    return next_state;
}
  