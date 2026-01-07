#include "state_machine.h"
#include "esp_log.h"

static const char *TAG = "state_machine";

/*----------------------主设备状态机----------------------*/
master_state_t master_state_machine(master_state_t cur_state,
                                    master_event_t event)
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
        if (event == EVT_PAIR_DONE) {
            next_state = MASTER_READY;
            ESP_LOGI(TAG, "MASTER: pairing done -> MASTER_READY");
        } else if (event == EVT_PAIR_TIMEOUT) {
            ESP_LOGW(TAG, "MASTER: pairing timeout -> MASTER_IDLE");
            next_state = MASTER_IDLE;
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
            next_state = MASTER_READY;
            ESP_LOGI(TAG, "MASTER: stop work -> MASTER_IDLE");
        }
        break;

    default:
        if (event == EVT_ERROR) {
            next_state = MASTER_ERROR;
            ESP_LOGE(TAG, "MASTER: error -> MASTER_ERROR");
        }
        break;
    }

    return next_state;
}





