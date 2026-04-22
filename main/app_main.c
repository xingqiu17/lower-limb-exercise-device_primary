/* Get Start Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "driver/gpio.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_wifi.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)

#endif

#include "espnow.h"
#include "espnow_storage.h"
#include "espnow_utils.h"

#include "audio_play.h"
#include "neopixel_ctrl.h"
#include "gpio_wakeup.h"

#include "espnow_sr.h"
#include "state_machine.h"
#include "esp_sleep.h"

static const char *TAG = "app_main";

#define APP_MAX_PLAYABLE_MUSIC     255
#define APP_ACTION_COUNT           4
#define APP_TRIGGER_LOW_TIME_MS    100
#define APP_PROMPT_DELAY_AFTER_STOP_MS 1000
#define APP_GPIO2_START_DELAY_MS   5000
#define APP_REST_TIME_MS           30000
#define APP_PLAYS_PER_ACTION       2
#define APP_ACTION34_INDICATOR_ON_MS 500
#define APP_ACTION34_INDICATOR_OFF_MS 500
#define APP_POWER_KEY_GPIO         GPIO_NUM_13
#define APP_POWER_KEY_SCAN_MS      20
#define APP_POWER_KEY_MIN_MS       30
#define APP_POWER_KEY_MAX_MS       1000
#define APP_SLAVE_POWER_ACK_TIMEOUT_MS 1000



typedef struct {
    uint16_t music_id;
    uint8_t trigger_mask;
} app_music_trigger_t;

static const app_music_trigger_t s_music_trigger_table[] = {
    {1, 0xFE},
    {2, 0xFD},
    {3, 0xFC},
    {4, 0xFB},
    {5, 0xFA},
    {6, 0xF9},
    {7, 0xF8},
    {8, 0xF7},
    {9, 0xF6},
    {10, 0xF5},
    {11, 0xF4},
    {12, 0xF3},
    {13, 0xF2},
};

#define APP_TRACK_COUNT ((uint8_t)(sizeof(s_music_trigger_table) / sizeof(s_music_trigger_table[0])))

_Static_assert((sizeof(s_music_trigger_table) / sizeof(s_music_trigger_table[0])) <= APP_MAX_PLAYABLE_MUSIC,
               "music trigger table supports up to 255 entries");

master_state_t m_state = MASTER_IDLE;
slave_state_t s_state[2] = {SLAVE_IDLE,SLAVE_IDLE};

static TimerHandle_t s_gpio2_start_delay_timer = NULL;
static TimerHandle_t s_gpio2_toggle_timer = NULL;
static bool s_action_running = false;
static bool s_gpio2_is_red = true;
static uint32_t s_gpio2_action_mode = 0;
static bool s_gpio2_blink_enabled = false;
static bool s_gpio2_indicator_started = false;
static volatile bool s_device_power_on = false;
static bool s_normal_services_started = false;
static bool s_wifi_driver_inited = false;
static bool s_event_loop_inited = false;
static QueueHandle_t s_power_key_evt_queue = NULL;
static TaskHandle_t s_pairing_task_handle = NULL;
static TaskHandle_t s_keepalive_task_handle = NULL;
static TaskHandle_t s_audio_evt_task_handle = NULL;

static esp_err_t app_start_normal_services(void);
static void app_stop_normal_services(void);
static void app_stop_user_visible_output_now(void);
static void app_clear_audio_event_queue(void);
static void app_prepare_clean_training_state(void);
static bool app_is_training_flow_allowed(void);
static bool app_delay_abort_on_shutdown(uint32_t delay_ms);

static const app_music_trigger_t *app_get_music_trigger(uint8_t track_index)
{
    if (track_index >= APP_TRACK_COUNT) {
        return NULL;
    }

    return &s_music_trigger_table[track_index];
}

static uint16_t app_get_music_id(uint8_t track_index)
{
    const app_music_trigger_t *track = app_get_music_trigger(track_index);
    if (track == NULL) {
        return 0;
    }

    return track->music_id;
}

static int16_t app_find_track_index_by_music_id(uint16_t music_id)
{
    for (uint8_t i = 0; i < APP_TRACK_COUNT; ++i) {
        if (s_music_trigger_table[i].music_id == music_id) {
            return (int16_t)i;
        }
    }

    return -1;
}

static uint16_t app_get_round_rest_prompt_music_id(uint16_t finished_action_music_id)
{
    if ((finished_action_music_id >= 1U) && (finished_action_music_id <= 4U)) {
        return (uint16_t)(finished_action_music_id + 4U);
    }

    return 0;
}

static uint16_t app_get_action_finish_prompt_music_id(uint16_t finished_action_music_id)
{
    switch (finished_action_music_id) {
    case 4:
        return 5;
    case 1:
        return 6;
    case 2:
        return 7;
    case 3:
        return 8;
    default:
        return 0;
    }
}

static esp_err_t app_play_music_by_id(uint16_t music_id)
{
    int16_t track_index = app_find_track_index_by_music_id(music_id);
    if (track_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    const app_music_trigger_t *track = app_get_music_trigger((uint8_t)track_index);
    if (track == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return audio_play_trigger_mask_once(track->trigger_mask, APP_TRIGGER_LOW_TIME_MS);
}

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

    if (action_mode == 3) {
        *phase_ms = red_phase ? APP_ACTION34_INDICATOR_ON_MS : APP_ACTION34_INDICATOR_OFF_MS;
        return true;
    }

    if (action_mode == 4) {
        *phase_ms = red_phase ? APP_ACTION34_INDICATOR_ON_MS : APP_ACTION34_INDICATOR_OFF_MS;
        return true;
    }

    return false;
}

static void app_gpio2_refresh_display(void)
{
    uint8_t indicator1_on = 0;
    uint8_t indicator1_r = 0;
    uint8_t indicator1_g = 0;
    uint8_t indicator1_b = 0;
    uint8_t indicator2_on = 0;
    uint8_t indicator2_r = 0;
    uint8_t indicator2_g = 0;
    uint8_t indicator2_b = 0;
    uint8_t active_action = 0;
    uint8_t status_on = 0;

    if (s_action_running && (s_gpio2_action_mode >= 1U) && (s_gpio2_action_mode <= APP_ACTION_COUNT)) {
        active_action = (uint8_t)s_gpio2_action_mode;
        status_on = 1;

        if (s_gpio2_blink_enabled && s_gpio2_indicator_started) {
            if ((s_gpio2_action_mode == 1U) || (s_gpio2_action_mode == 2U)) {
                if (s_gpio2_is_red) {
                    indicator1_on = 1;
                    indicator1_r = 255;
                } else {
                    indicator2_on = 1;
                    indicator2_g = 255;
                }
            } else if ((s_gpio2_action_mode == 3U) || (s_gpio2_action_mode == 4U)) {
                if (s_gpio2_is_red) {
                    indicator1_on = 1;
                    indicator1_r = 255;
                    indicator2_on = 1;
                    indicator2_r = 255;
                }
            }
        }
    }

    (void)neopixel_ctrl_set_gpio2_action_panel(indicator1_on,
                                               indicator1_r,
                                               indicator1_g,
                                               indicator1_b,
                                               indicator2_on,
                                               indicator2_r,
                                               indicator2_g,
                                               indicator2_b,
                                               active_action,
                                               status_on);
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

    s_gpio2_indicator_started = true;
    s_gpio2_is_red = true;
    app_gpio2_refresh_display();
    app_gpio2_schedule_next_toggle();
}

static void app_gpio2_toggle_timer_cb(TimerHandle_t timer)
{
    (void)timer;

    if (!s_action_running || !s_gpio2_blink_enabled) {
        return;
    }

    s_gpio2_is_red = !s_gpio2_is_red;
    app_gpio2_refresh_display();

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
        s_gpio2_indicator_started = false;

        if (s_gpio2_start_delay_timer != NULL) {
            (void)xTimerStop(s_gpio2_start_delay_timer, 0);
        }
        if (s_gpio2_toggle_timer != NULL) {
            (void)xTimerStop(s_gpio2_toggle_timer, 0);
        }

        app_gpio2_refresh_display();

        if (s_gpio2_blink_enabled && s_gpio2_start_delay_timer != NULL) {
            (void)xTimerStart(s_gpio2_start_delay_timer, 0);
        }

        return;
    }

    s_action_running = false;
    s_gpio2_action_mode = 0;
    s_gpio2_blink_enabled = false;
    s_gpio2_indicator_started = false;
    if (s_gpio2_start_delay_timer != NULL) {
        (void)xTimerStop(s_gpio2_start_delay_timer, 0);
    }
    if (s_gpio2_toggle_timer != NULL) {
        (void)xTimerStop(s_gpio2_toggle_timer, 0);
    }
    app_gpio2_refresh_display();
}

static esp_err_t app_sync_action_and_play(uint8_t track_index)
{
    const app_music_trigger_t *track = app_get_music_trigger(track_index);
    if (track == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t action_mode = track->music_id;
    master_set_current_action_mode(action_mode);

    esp_err_t send_ret = master_send_action_to_ready_slaves(action_mode);
    if (send_ret != ESP_OK) {
        ESP_LOGW(TAG,
                 "MASTER: send action=%lu incomplete, err=%s",
                 (unsigned long)action_mode,
                 esp_err_to_name(send_ret));
    }

    esp_err_t trigger_ret = audio_play_trigger_mask_once(track->trigger_mask, APP_TRIGGER_LOW_TIME_MS);
    if (trigger_ret != ESP_OK) {
        master_set_current_action_mode(0);
        return trigger_ret;
    }

    ESP_LOGI(TAG,
             "sync action+music ok, music=%u, mask=0x%02X, action=%lu",
             (unsigned)track->music_id,
             (unsigned)track->trigger_mask,
             (unsigned long)action_mode);
    return ESP_OK;
}

static esp_err_t app_start_action_flow(uint8_t track_index)
{
    const app_music_trigger_t *track = app_get_music_trigger(track_index);
    if (track == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = app_sync_action_and_play(track_index);
    if (ret != ESP_OK) {
        return ret;
    }

    app_set_action_running(true, track->music_id);
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

static int16_t app_find_next_action(uint8_t current_action_index, const bool *completed_track)
{
    for (uint8_t offset = 1; offset <= APP_ACTION_COUNT; ++offset) {
        uint8_t candidate = (current_action_index + offset) % APP_ACTION_COUNT;
        if (!completed_track[candidate]) {
            return (int16_t)candidate;
        }
    }

    return -1;
}

static bool app_is_all_tracks_completed(const bool *completed_track)
{
    for (uint8_t i = 0; i < APP_ACTION_COUNT; ++i) {
        if (!completed_track[i]) {
            return false;
        }
    }

    return true;
}

static void app_reset_sequence_state(bool *sequence_active,
                                     bool *action_playing,
                                     bool *resting,
                                     bool *completed_track,
                                     uint8_t *current_action_io,
                                     uint8_t *current_action_round,
                                     TickType_t *rest_until_tick,
                                     int8_t *first_pressed_button)
{
    app_stop_current_action_flow();
    *sequence_active = false;
    *action_playing = false;
    *resting = false;
    for (uint8_t i = 0; i < APP_ACTION_COUNT; ++i) {
        completed_track[i] = false;
    }
    *current_action_io = 0;
    *current_action_round = 0;
    *rest_until_tick = 0;
    *first_pressed_button = -1;
}

static bool app_is_training_flow_allowed(void)
{
    return s_device_power_on && !master_is_shutdown_in_progress();
}

static bool app_delay_abort_on_shutdown(uint32_t delay_ms)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(delay_ms);
    while (true) {
        if (!app_is_training_flow_allowed()) {
            return false;
        }

        TickType_t now = xTaskGetTickCount();
        if ((int32_t)(deadline - now) <= 0) {
            return true;
        }

        TickType_t remain = deadline - now;
        TickType_t step = remain > pdMS_TO_TICKS(20) ? pdMS_TO_TICKS(20) : remain;
        vTaskDelay(step);
    }
}



static void audio_play_event_task(void *arg)
{
    QueueHandle_t evt_queue = audio_play_get_event_queue();
    audio_play_event_t evt;
    bool sequence_active = false;
    bool action_playing = false;
    bool resting = false;
    bool completed_track[APP_ACTION_COUNT] = {0};
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
        // 关机态只做事件清理，不执行业务流程。
        if (!s_device_power_on) {
            if (sequence_active || action_playing || resting) {
                app_reset_sequence_state(&sequence_active,
                                         &action_playing,
                                         &resting,
                                         completed_track,
                                         &current_action_io,
                                         &current_action_round,
                                         &rest_until_tick,
                                         &first_pressed_button);
            }

            if (xQueueReceive(evt_queue, &evt, pdMS_TO_TICKS(100)) == pdTRUE) {
                continue;
            }
            continue;
        }

        if (xQueueReceive(evt_queue, &evt, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (!app_is_training_flow_allowed()) {
                app_reset_sequence_state(&sequence_active,
                                         &action_playing,
                                         &resting,
                                         completed_track,
                                         &current_action_io,
                                         &current_action_round,
                                         &rest_until_tick,
                                         &first_pressed_button);
                continue;
            }

            if (evt.type == AUDIO_PLAY_EVENT_BUTTON_PRESSED) {
                uint8_t logical_io = evt.logical_io;

                if (logical_io >= APP_ACTION_COUNT) {
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
                for (uint8_t i = 0; i < APP_ACTION_COUNT; ++i) {
                    completed_track[i] = false;
                }
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
                                             completed_track,
                                             &current_action_io,
                                             &current_action_round,
                                             &rest_until_tick,
                                             &first_pressed_button);
                    continue;
                }

                action_playing = true;
                ESP_LOGI(TAG,
                         "button%u pressed, start music%u round%u",
                         evt.button_index,
                         (unsigned)app_get_music_id(current_action_io),
                         (unsigned)(current_action_round + 1U));
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

                    uint16_t finished_action_music_id = app_get_music_id(current_action_io);
                    uint16_t prompt_music_id = app_get_round_rest_prompt_music_id(finished_action_music_id);
                    if (prompt_music_id != 0) {
                        if (!app_delay_abort_on_shutdown(APP_PROMPT_DELAY_AFTER_STOP_MS)) {
                            app_reset_sequence_state(&sequence_active,
                                                     &action_playing,
                                                     &resting,
                                                     completed_track,
                                                     &current_action_io,
                                                     &current_action_round,
                                                     &rest_until_tick,
                                                     &first_pressed_button);
                            continue;
                        }

                        if (!app_is_training_flow_allowed()) {
                            app_reset_sequence_state(&sequence_active,
                                                     &action_playing,
                                                     &resting,
                                                     completed_track,
                                                     &current_action_io,
                                                     &current_action_round,
                                                     &rest_until_tick,
                                                     &first_pressed_button);
                            continue;
                        }

                        esp_err_t prompt_ret = app_play_music_by_id(prompt_music_id);
                        if (prompt_ret != ESP_OK) {
                            ESP_LOGW(TAG,
                                     "play rest prompt music%u failed after action%u round1: %s",
                                     (unsigned)prompt_music_id,
                                     (unsigned)finished_action_music_id,
                                     esp_err_to_name(prompt_ret));
                        } else {
                            ESP_LOGI(TAG,
                                     "play rest prompt music%u after action%u round1",
                                     (unsigned)prompt_music_id,
                                     (unsigned)finished_action_music_id);
                        }
                    }

                    ESP_LOGI(TAG,
                             "music%u round%u done, rest %ums then repeat",
                             (unsigned)app_get_music_id(current_action_io),
                             (unsigned)current_action_round,
                             (unsigned)APP_REST_TIME_MS);
                    continue;
                }

                completed_track[current_action_io] = true;

                if (app_is_all_tracks_completed(completed_track)) {
                    ESP_LOGI(TAG, "all actions done after button%u, stop sequence", (unsigned)first_pressed_button);
                    app_reset_sequence_state(&sequence_active,
                                             &action_playing,
                                             &resting,
                                             completed_track,
                                             &current_action_io,
                                             &current_action_round,
                                             &rest_until_tick,
                                             &first_pressed_button);
                    continue;
                }

                int16_t next_action = app_find_next_action(current_action_io, completed_track);
                if (next_action < 0) {
                    ESP_LOGW(TAG, "next action not found, stop sequence");
                    app_reset_sequence_state(&sequence_active,
                                             &action_playing,
                                             &resting,
                                             completed_track,
                                             &current_action_io,
                                             &current_action_round,
                                             &rest_until_tick,
                                             &first_pressed_button);
                    continue;
                }

                uint16_t finished_action_music_id = app_get_music_id(current_action_io);
                uint16_t prompt_music_id = app_get_action_finish_prompt_music_id(finished_action_music_id);
                if (prompt_music_id != 0) {
                    if (!app_delay_abort_on_shutdown(APP_PROMPT_DELAY_AFTER_STOP_MS)) {
                        app_reset_sequence_state(&sequence_active,
                                                 &action_playing,
                                                 &resting,
                                                 completed_track,
                                                 &current_action_io,
                                                 &current_action_round,
                                                 &rest_until_tick,
                                                 &first_pressed_button);
                        continue;
                    }

                    if (!app_is_training_flow_allowed()) {
                        app_reset_sequence_state(&sequence_active,
                                                 &action_playing,
                                                 &resting,
                                                 completed_track,
                                                 &current_action_io,
                                                 &current_action_round,
                                                 &rest_until_tick,
                                                 &first_pressed_button);
                        continue;
                    }

                    esp_err_t prompt_ret = app_play_music_by_id(prompt_music_id);
                    if (prompt_ret != ESP_OK) {
                        ESP_LOGW(TAG,
                                 "play prepare prompt music%u failed after action%u: %s",
                                 (unsigned)prompt_music_id,
                                 (unsigned)finished_action_music_id,
                                 esp_err_to_name(prompt_ret));
                    } else {
                        ESP_LOGI(TAG,
                                 "play prepare prompt music%u after action%u",
                                 (unsigned)prompt_music_id,
                                 (unsigned)finished_action_music_id);
                    }
                }

                current_action_io = (uint8_t)next_action;
                current_action_round = 0;
                resting = true;
                rest_until_tick = xTaskGetTickCount() + pdMS_TO_TICKS(APP_REST_TIME_MS);
                ESP_LOGI(TAG, "music switch to %u after rest %ums", (unsigned)app_get_music_id(current_action_io), (unsigned)APP_REST_TIME_MS);
            }
        }

        if (sequence_active && resting) {
            if (!app_is_training_flow_allowed()) {
                app_reset_sequence_state(&sequence_active,
                                         &action_playing,
                                         &resting,
                                         completed_track,
                                         &current_action_io,
                                         &current_action_round,
                                         &rest_until_tick,
                                         &first_pressed_button);
                continue;
            }

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
                                             completed_track,
                                             &current_action_io,
                                             &current_action_round,
                                             &rest_until_tick,
                                             &first_pressed_button);
                    continue;
                }

                resting = false;
                action_playing = true;
                ESP_LOGI(TAG,
                         "start music%u round%u after rest",
                         (unsigned)app_get_music_id(current_action_io),
                         (unsigned)(current_action_round + 1U));
            }
        }
    }
}






static esp_err_t app_wifi_init(void)
{
    if (!s_event_loop_inited) {
        esp_err_t loop_ret = esp_event_loop_create_default();
        if ((loop_ret != ESP_OK) && (loop_ret != ESP_ERR_INVALID_STATE)) {
            return loop_ret;
        }
        s_event_loop_inited = true;
    }

    if (!s_wifi_driver_inited) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

        ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi set mode failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi set storage failed");
        s_wifi_driver_inited = true;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "wifi set ps failed");

    esp_err_t start_ret = esp_wifi_start();
    if ((start_ret != ESP_OK) && (start_ret != ESP_ERR_WIFI_CONN)) {
        ESP_RETURN_ON_ERROR(start_ret, TAG, "wifi start failed");
    }

    return ESP_OK;
}

static void app_power_key_task(void *arg)
{
    (void)arg;

    while (true) {
        if (!s_device_power_on) {
            vTaskDelay(pdMS_TO_TICKS(APP_POWER_KEY_SCAN_MS));
            continue;
        }

        if (lp_power_key_poll_short_press(APP_POWER_KEY_MIN_MS, APP_POWER_KEY_MAX_MS)) {
            uint8_t evt = 1;
            if (s_power_key_evt_queue != NULL) {
                (void)xQueueSend(s_power_key_evt_queue, &evt, 0);
            }
        }
    }
}

static void app_stop_user_visible_output_now(void)
{
    // 用户触发关机后，先立刻停掉可见/可听输出，保证体感无延迟。
    app_stop_current_action_flow();
    esp_err_t stop_music_ret = audio_play_stop_playback();
    if ((stop_music_ret != ESP_OK) && (stop_music_ret != ESP_ERR_INVALID_STATE)) {
        ESP_LOGW(TAG, "stop music failed: %s", esp_err_to_name(stop_music_ret));
    }

    (void)audio_play_set_io_mask(0xFF);
    (void)neopixel_ctrl_set_gpio1_progress_red(0);
    (void)neopixel_ctrl_set_gpio2_action_panel(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

static void app_clear_audio_event_queue(void)
{
    QueueHandle_t evt_queue = audio_play_get_event_queue();
    if (evt_queue == NULL) {
        return;
    }

    audio_play_event_t evt;
    uint32_t dropped = 0;
    while (xQueueReceive(evt_queue, &evt, 0) == pdTRUE) {
        dropped++;
    }

    if (dropped > 0) {
        ESP_LOGI(TAG, "clear stale audio events: %lu", (unsigned long)dropped);
    }
}

static void app_prepare_clean_training_state(void)
{
    master_set_current_action_mode(0);
    app_set_action_running(false, 0);
    app_clear_audio_event_queue();
}

static esp_err_t app_start_normal_services(void)
{
    if (s_normal_services_started) {
        return ESP_OK;
    }

    master_set_shutdown_in_progress(false);

    ESP_RETURN_ON_ERROR(app_wifi_init(), TAG, "wifi init/start failed");

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(espnow_init(&espnow_config), TAG, "espnow init failed");

    if (master_evt_queue == NULL) {
        master_evt_queue = xQueueCreate(8, sizeof(master_evt_msg_t));
        ESP_RETURN_ON_FALSE(master_evt_queue != NULL, ESP_ERR_NO_MEM, TAG, "master_evt_queue create failed");
    }

    ESP_RETURN_ON_ERROR(audio_play_set_encoder_enabled(true), TAG, "enable encoder volume failed");
    ESP_RETURN_ON_ERROR(audio_play_set_io_mask(0xFF), TAG, "set audio io mask failed");
    ESP_RETURN_ON_ERROR(espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_DATA, true, master_receive_handle), TAG, "set espnow data callback failed");

    BaseType_t task_ok = xTaskCreate(ms_pairing_task,
                                     "ms_pairing",
                                     4096,
                                     NULL,
                                     4,
                                     &s_pairing_task_handle);
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_FAIL, TAG, "create ms_pairing_task failed");

    task_ok = xTaskCreate(master_keepalive_monitor_task,
                          "ms_keepalive",
                          3072,
                          NULL,
                          3,
                          &s_keepalive_task_handle);
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_FAIL, TAG, "create keepalive task failed");

    if (s_audio_evt_task_handle == NULL) {
        task_ok = xTaskCreate(audio_play_event_task,
                              "audio_evt",
                              4096,
                              NULL,
                              3,
                              &s_audio_evt_task_handle);
        ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_FAIL, TAG, "create audio event task failed");
    }

    s_normal_services_started = true;

    // 主机开机后，向已就绪从机广播开机语义（data=1）。
    esp_err_t pwr_on_ret = master_send_power_manage_to_ready_slaves(1);
    if ((pwr_on_ret != ESP_OK) && (pwr_on_ret != ESP_ERR_NOT_FOUND)) {
        ESP_LOGW(TAG, "send slave power on cmd failed: %s", esp_err_to_name(pwr_on_ret));
    }

    ESP_LOGI(TAG, "device power on: normal services started");
    return ESP_OK;
}

static void app_stop_normal_services(void)
{
    if (!s_normal_services_started) {
        return;
    }

    app_stop_current_action_flow();
    esp_err_t stop_music_ret = audio_play_stop_playback();
    if ((stop_music_ret != ESP_OK) && (stop_music_ret != ESP_ERR_INVALID_STATE)) {
        ESP_LOGW(TAG, "stop music on power off failed: %s", esp_err_to_name(stop_music_ret));
    }
    (void)audio_play_set_encoder_enabled(false);
    (void)audio_play_set_io_mask(0xFF);

    if (s_keepalive_task_handle != NULL) {
        vTaskDelete(s_keepalive_task_handle);
        s_keepalive_task_handle = NULL;
    }

    if (s_pairing_task_handle != NULL) {
        vTaskDelete(s_pairing_task_handle);
        s_pairing_task_handle = NULL;
    }

    (void)espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_DATA, false, NULL);
    (void)espnow_deinit();
    (void)esp_wifi_stop();

    if (master_evt_queue != NULL) {
        vQueueDelete(master_evt_queue);
        master_evt_queue = NULL;
    }

    s_normal_services_started = false;
    ESP_LOGI(TAG, "device power off: normal services stopped");
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
    ESP_ERROR_CHECK(audio_play_set_encoder_enabled(false));
    ESP_ERROR_CHECK(lp_power_key_init(APP_POWER_KEY_GPIO));

    s_power_key_evt_queue = xQueueCreate(4, sizeof(uint8_t));
    ESP_ERROR_CHECK(s_power_key_evt_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    BaseType_t key_task_ok = xTaskCreate(app_power_key_task,
                                         "power_key",
                                         2048,
                                         NULL,
                                         5,
                                         NULL);
    ESP_ERROR_CHECK(key_task_ok == pdPASS ? ESP_OK : ESP_FAIL);

    // 上电默认进入轻睡眠，GPIO13短按唤醒开机。
    s_device_power_on = false;
    ESP_LOGI(TAG, "init done, enter light sleep automatically");

    while (true) {
        if (!s_device_power_on) {
            lp_sleep_result_t sleep_result = {0};
            esp_err_t sleep_ret = lp_enter_light_sleep(&sleep_result);
            if (sleep_ret != ESP_OK) {
                ESP_LOGE(TAG, "enter light sleep failed: %s", esp_err_to_name(sleep_ret));
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            if (sleep_result.wakeup_cause != ESP_SLEEP_WAKEUP_GPIO) {
                ESP_LOGW(TAG, "non-GPIO wakeup, re-enter light sleep");
                continue;
            }

            esp_err_t start_ret = app_start_normal_services();
            if (start_ret != ESP_OK) {
                ESP_LOGE(TAG, "power on failed after wakeup: %s", esp_err_to_name(start_ret));
                continue;
            }

            app_prepare_clean_training_state();
            s_device_power_on = true;
            ESP_LOGI(TAG, "device power on by GPIO13");
            continue;
        }

        uint8_t evt = 0;
        if (xQueueReceive(s_power_key_evt_queue, &evt, portMAX_DELAY) == pdTRUE) {
            master_set_shutdown_in_progress(true);

            // 1) 用户可见输出立即停止（零体感延迟）
            app_stop_user_visible_output_now();
            s_device_power_on = false;

            // 2) 主机向从机发关机指令（data=0），并等待从机确认（data=2）
            esp_err_t pwr_off_send_ret = master_send_power_manage_to_ready_slaves(0);
            bool got_ack = false;
            if (pwr_off_send_ret == ESP_OK) {
                got_ack = master_wait_slave_power_ack(APP_SLAVE_POWER_ACK_TIMEOUT_MS);
            } else if (pwr_off_send_ret != ESP_ERR_NOT_FOUND) {
                ESP_LOGW(TAG, "send slave power off cmd failed: %s", esp_err_to_name(pwr_off_send_ret));
            }

            if (!got_ack) {
                ESP_LOGW(TAG, "slave power off ack timeout, continue local shutdown");
            }

            app_stop_normal_services();
            app_prepare_clean_training_state();
            ESP_LOGI(TAG, "device power off, services stopped");
        }
    }
}
