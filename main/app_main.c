/* Get Start Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

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
#include "neopixel_ctrl.h"

#include "espnow_sr.h"
#include "state_machine.h"

static const char *TAG = "app_main";

#define APP_TRACK_COUNT            4
#define APP_TRACK_PLAYED_MASK_ALL  ((1U << APP_TRACK_COUNT) - 1U)
#define APP_TRIGGER_LOW_TIME_MS    100
#define APP_GPIO2_START_DELAY_MS   5000
#define APP_REST_TIME_MS           30000
#define APP_PLAYS_PER_ACTION       2

master_state_t m_state = MASTER_IDLE;
slave_state_t s_state[2] = {SLAVE_IDLE,SLAVE_IDLE};

static TimerHandle_t s_gpio2_start_delay_timer = NULL;
static TimerHandle_t s_gpio2_toggle_timer = NULL;
static bool s_action_running = false;
static bool s_gpio2_is_red = true;
static uint32_t s_gpio2_action_mode = 0;
static bool s_gpio2_blink_enabled = false;

static bool app_gpio2_get_phase_ms(uint32_t action_mode, bool red_phase, uint32_t *phase_ms)
{
    if (phase_ms == NULL) {
        return false;
    }

    if (action_mode == 1) {
        *phase_ms = 3500;
        return true;
    }

    if (action_mode == 2) {
        *phase_ms = red_phase ? 4000 : 1000;
        return true;
    }

    return false;
}

static void app_gpio2_schedule_next_toggle(void)
{
    uint32_t phase_ms = 0;

    if (!s_action_running || !s_gpio2_blink_enabled || s_gpio2_toggle_timer == NULL) {
        return;
    }

    if (!app_gpio2_get_phase_ms(s_gpio2_action_mode, s_gpio2_is_red, &phase_ms)) {
        return;
    }

    (void)xTimerChangePeriod(s_gpio2_toggle_timer, pdMS_TO_TICKS(phase_ms), 0);
    (void)xTimerStart(s_gpio2_toggle_timer, 0);
}

static void app_gpio2_start_delay_timer_cb(TimerHandle_t timer)
{
    (void)timer;

    if (!s_action_running || !s_gpio2_blink_enabled) {
        return;
    }

    s_gpio2_is_red = true;
    (void)neopixel_ctrl_set_gpio2_all_rgb(255, 0, 0);
    app_gpio2_schedule_next_toggle();
}

static void app_gpio2_toggle_timer_cb(TimerHandle_t timer)
{
    (void)timer;

    if (!s_action_running || !s_gpio2_blink_enabled) {
        return;
    }

    s_gpio2_is_red = !s_gpio2_is_red;
    if (s_gpio2_is_red) {
        (void)neopixel_ctrl_set_gpio2_all_rgb(255, 0, 0);
    } else {
        (void)neopixel_ctrl_set_gpio2_all_rgb(0, 255, 0);
    }

    app_gpio2_schedule_next_toggle();
}

static void app_set_action_running(bool running, uint32_t action_mode)
{
    if (running) {
        uint32_t phase_ms = 0;

        s_action_running = true;
        s_gpio2_action_mode = action_mode;
        s_gpio2_is_red = true;
        s_gpio2_blink_enabled = app_gpio2_get_phase_ms(action_mode, true, &phase_ms);

        if (s_gpio2_start_delay_timer != NULL) {
            (void)xTimerStop(s_gpio2_start_delay_timer, 0);
        }
        if (s_gpio2_toggle_timer != NULL) {
            (void)xTimerStop(s_gpio2_toggle_timer, 0);
        }

        (void)neopixel_ctrl_set_gpio2_all_rgb(0, 0, 0);

        if (s_gpio2_blink_enabled && s_gpio2_start_delay_timer != NULL) {
            (void)xTimerStart(s_gpio2_start_delay_timer, 0);
        }

        return;
    }

    s_action_running = false;
    s_gpio2_action_mode = 0;
    s_gpio2_blink_enabled = false;
    if (s_gpio2_start_delay_timer != NULL) {
        (void)xTimerStop(s_gpio2_start_delay_timer, 0);
    }
    if (s_gpio2_toggle_timer != NULL) {
        (void)xTimerStop(s_gpio2_toggle_timer, 0);
    }
    (void)neopixel_ctrl_set_gpio2_all_rgb(0, 0, 0);
}

static esp_err_t app_sync_action_and_play(uint8_t logical_io)
{
    if (logical_io >= APP_TRACK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t action_mode = (uint32_t)logical_io + 1U;
    master_set_current_action_mode(action_mode);

    esp_err_t send_ret = master_send_action_to_ready_slaves(action_mode);
    if (send_ret != ESP_OK) {
        ESP_LOGW(TAG,
                 "MASTER: send action=%lu incomplete, err=%s",
                 (unsigned long)action_mode,
                 esp_err_to_name(send_ret));
    }

    esp_err_t trigger_ret = audio_play_trigger_once(logical_io, APP_TRIGGER_LOW_TIME_MS);
    if (trigger_ret != ESP_OK) {
        master_set_current_action_mode(0);
        return trigger_ret;
    }

    ESP_LOGI(TAG,
             "sync action+music ok, action=%lu, IO%u",
             (unsigned long)action_mode,
             (unsigned)logical_io);
    return ESP_OK;
}

static esp_err_t app_start_action_flow(uint8_t logical_io)
{
    esp_err_t ret = app_sync_action_and_play(logical_io);
    if (ret != ESP_OK) {
        return ret;
    }

    app_set_action_running(true, (uint32_t)logical_io + 1U);
    return ESP_OK;
}

static void app_stop_current_action_flow(void)
{
    esp_err_t stop_send_ret = master_send_action_to_ready_slaves(0);
    if (stop_send_ret != ESP_OK) {
        ESP_LOGW(TAG, "send stop action to slaves failed: %s", esp_err_to_name(stop_send_ret));
    }

    master_set_current_action_mode(0);
    app_set_action_running(false, 0);
}

static int8_t app_find_next_action(uint8_t current_action_io, uint8_t completed_action_mask)
{
    for (uint8_t offset = 1; offset <= APP_TRACK_COUNT; ++offset) {
        uint8_t candidate = (current_action_io + offset) % APP_TRACK_COUNT;
        if ((completed_action_mask & (1U << candidate)) == 0U) {
            return (int8_t)candidate;
        }
    }

    return -1;
}

static void app_reset_sequence_state(bool *sequence_active,
                                     bool *action_playing,
                                     bool *resting,
                                     uint8_t *completed_action_mask,
                                     uint8_t *current_action_io,
                                     uint8_t *current_action_round,
                                     TickType_t *rest_until_tick,
                                     int8_t *first_pressed_button)
{
    app_stop_current_action_flow();
    *sequence_active = false;
    *action_playing = false;
    *resting = false;
    *completed_action_mask = 0;
    *current_action_io = 0;
    *current_action_round = 0;
    *rest_until_tick = 0;
    *first_pressed_button = -1;
}



static void audio_play_event_task(void *arg)
{
    QueueHandle_t evt_queue = audio_play_get_event_queue();
    audio_play_event_t evt;
    bool sequence_active = false;
    bool action_playing = false;
    bool resting = false;
    uint8_t completed_action_mask = 0;
    uint8_t current_action_io = 0;
    uint8_t current_action_round = 0;
    TickType_t rest_until_tick = 0;
    int8_t first_pressed_button = -1;

    if (evt_queue == NULL) {
        ESP_LOGE(TAG, "audio_play event queue is null");
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        if (xQueueReceive(evt_queue, &evt, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (evt.type == AUDIO_PLAY_EVENT_BUTTON_PRESSED) {
                uint8_t logical_io = evt.logical_io;

                if (logical_io >= APP_TRACK_COUNT) {
                    ESP_LOGW(TAG, "ignore invalid button event: button=%u logical_io=%u", evt.button_index, logical_io);
                    continue;
                }

                if (sequence_active) {
                    ESP_LOGI(TAG, "button%u ignored, sequence is running/resting", evt.button_index);
                    continue;
                }

                sequence_active = true;
                action_playing = false;
                resting = false;
                completed_action_mask = 0;
                current_action_io = logical_io;
                current_action_round = 0;
                rest_until_tick = 0;
                first_pressed_button = (int8_t)evt.button_index;

                esp_err_t play_ret = app_start_action_flow(current_action_io);
                if (play_ret != ESP_OK) {
                    ESP_LOGE(TAG, "button%u start sequence IO%u failed: %s", evt.button_index, current_action_io, esp_err_to_name(play_ret));
                    app_reset_sequence_state(&sequence_active,
                                             &action_playing,
                                             &resting,
                                             &completed_action_mask,
                                             &current_action_io,
                                             &current_action_round,
                                             &rest_until_tick,
                                             &first_pressed_button);
                    continue;
                }

                action_playing = true;
                ESP_LOGI(TAG, "button%u pressed, start action%u round%u", evt.button_index, (unsigned)(current_action_io + 1U), (unsigned)(current_action_round + 1U));
                continue;
            }

            if (evt.type == AUDIO_PLAY_EVENT_BUSY_RISING) {
                ESP_LOGI(TAG, "recv BUSY rising event, tick=%lu", (unsigned long)evt.tick);

                if (!sequence_active || !action_playing) {
                    continue;
                }

                app_stop_current_action_flow();
                action_playing = false;

                if ((current_action_round + 1U) < APP_PLAYS_PER_ACTION) {
                    current_action_round++;
                    resting = true;
                    rest_until_tick = xTaskGetTickCount() + pdMS_TO_TICKS(APP_REST_TIME_MS);
                    ESP_LOGI(TAG, "action%u round%u done, rest %ums then repeat", (unsigned)(current_action_io + 1U), (unsigned)current_action_round, (unsigned)APP_REST_TIME_MS);
                    continue;
                }

                completed_action_mask |= (1U << current_action_io);

                if (completed_action_mask == APP_TRACK_PLAYED_MASK_ALL) {
                    ESP_LOGI(TAG, "all actions done after button%u, stop sequence", (unsigned)first_pressed_button);
                    app_reset_sequence_state(&sequence_active,
                                             &action_playing,
                                             &resting,
                                             &completed_action_mask,
                                             &current_action_io,
                                             &current_action_round,
                                             &rest_until_tick,
                                             &first_pressed_button);
                    continue;
                }

                int8_t next_action = app_find_next_action(current_action_io, completed_action_mask);
                if (next_action < 0) {
                    ESP_LOGW(TAG, "next action not found, stop sequence");
                    app_reset_sequence_state(&sequence_active,
                                             &action_playing,
                                             &resting,
                                             &completed_action_mask,
                                             &current_action_io,
                                             &current_action_round,
                                             &rest_until_tick,
                                             &first_pressed_button);
                    continue;
                }

                current_action_io = (uint8_t)next_action;
                current_action_round = 0;
                resting = true;
                rest_until_tick = xTaskGetTickCount() + pdMS_TO_TICKS(APP_REST_TIME_MS);
                ESP_LOGI(TAG, "action switch to %u after rest %ums", (unsigned)(current_action_io + 1U), (unsigned)APP_REST_TIME_MS);
            }
        }

        if (sequence_active && resting) {
            TickType_t now = xTaskGetTickCount();
            if ((int32_t)(now - rest_until_tick) >= 0) {
                esp_err_t play_ret = app_start_action_flow(current_action_io);
                if (play_ret != ESP_OK) {
                    ESP_LOGE(TAG, "start action%u round%u after rest failed: %s",
                             (unsigned)(current_action_io + 1U),
                             (unsigned)(current_action_round + 1U),
                             esp_err_to_name(play_ret));
                    app_reset_sequence_state(&sequence_active,
                                             &action_playing,
                                             &resting,
                                             &completed_action_mask,
                                             &current_action_io,
                                             &current_action_round,
                                             &rest_until_tick,
                                             &first_pressed_button);
                    continue;
                }

                resting = false;
                action_playing = true;
                ESP_LOGI(TAG, "start action%u round%u after rest", (unsigned)(current_action_io + 1U), (unsigned)(current_action_round + 1U));
            }
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

    ESP_ERROR_CHECK(neopixel_ctrl_init());
    ESP_ERROR_CHECK(neopixel_ctrl_clear_all());
    ESP_ERROR_CHECK(neopixel_ctrl_set_gpio1_progress_red(0));

    s_gpio2_start_delay_timer = xTimerCreate("gpio2_delay_tmr",
                                             pdMS_TO_TICKS(APP_GPIO2_START_DELAY_MS),
                                             pdFALSE,
                                             NULL,
                                             app_gpio2_start_delay_timer_cb);
    ESP_ERROR_CHECK(s_gpio2_start_delay_timer != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    s_gpio2_toggle_timer = xTimerCreate("gpio2_color_tmr",
                                        pdMS_TO_TICKS(1000),
                                        pdFALSE,
                                        NULL,
                                        app_gpio2_toggle_timer_cb);
    ESP_ERROR_CHECK(s_gpio2_toggle_timer != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(audio_play_init());

    app_wifi_init();

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_init(&espnow_config);
    
    master_evt_queue = xQueueCreate(8, sizeof(master_evt_msg_t));

    ESP_ERROR_CHECK(audio_play_set_io_mask(0xFF));

    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_DATA, true, master_receive_handle);
    //初始化完成，开始配对

    xTaskCreate(ms_pairing_task,
            "ms_pairing",
            4096,
            NULL,
            4,
            NULL);



    xTaskCreate(audio_play_event_task,
        "audio_evt",
        4096,
        NULL,
        3,
        NULL);




    
}
