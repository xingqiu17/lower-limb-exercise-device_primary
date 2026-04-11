#include "audio_play.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "dysv_uart_adapter.h"
#include "driver/pulse_cnt.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"

#define AUDIO_PLAY_BUSY_MONITOR_GPIO          GPIO_NUM_8
#define AUDIO_PLAY_EVENT_QUEUE_LEN            8
#define AUDIO_PLAY_BUSY_MONITOR_TASK_STACK    2048
#define AUDIO_PLAY_BUSY_MONITOR_TASK_PRIO     4
#define AUDIO_PLAY_BUSY_MONITOR_PERIOD_MS     10
#define AUDIO_PLAY_BUTTON_MONITOR_TASK_STACK  2048
#define AUDIO_PLAY_BUTTON_MONITOR_TASK_PRIO   4
#define AUDIO_PLAY_BUTTON_MONITOR_PERIOD_MS   10
#define AUDIO_PLAY_PCNT_HIGH_LIMIT            32767
#define AUDIO_PLAY_PCNT_LOW_LIMIT             -32768
#define AUDIO_PLAY_EC11_A                GPIO_NUM_10
#define AUDIO_PLAY_EC11_B                GPIO_NUM_9
#define AUDIO_PLAY_PCNT_TASK_STACK            4096
#define AUDIO_PLAY_PCNT_TASK_PRIO             4
#define AUDIO_PLAY_PCNT_REPORT_PERIOD_MS      20
#define AUDIO_PLAY_VOLUME_MIN                 0
#define AUDIO_PLAY_VOLUME_MAX                 30
#define AUDIO_PLAY_VOLUME_DEFAULT             15
#define AUDIO_PLAY_ENCODER_COUNTS_PER_STEP    4
#define AUDIO_PLAY_ENCODER_CW_POSITIVE        1
#define AUDIO_PLAY_VOLUME_SAVE_IDLE_MS        3000
#define AUDIO_PLAY_NVS_NAMESPACE              "audio_play"
#define AUDIO_PLAY_NVS_KEY_VOLUME             "volume"

static const char *TAG = "audio_play";

static const gpio_num_t s_button_gpio_map[AUDIO_PLAY_BUTTON_NUM] = {
	GPIO_NUM_7,
	GPIO_NUM_6,
	GPIO_NUM_5,
	GPIO_NUM_4,
};

static QueueHandle_t s_event_queue = NULL;
static TaskHandle_t s_busy_monitor_task_handle = NULL;
static TaskHandle_t s_button_monitor_task_handle = NULL;
static TaskHandle_t s_pcnt_monitor_task_handle = NULL;
static pcnt_unit_handle_t s_pcnt_unit = NULL;
static pcnt_channel_handle_t s_pcnt_chan_a = NULL;
static pcnt_channel_handle_t s_pcnt_chan_b = NULL;
static bool s_is_initialized = false;
static uint8_t s_shadow_io_mask = 0xFF;
static int s_current_volume = AUDIO_PLAY_VOLUME_DEFAULT;
static int s_encoder_accum_counts = 0;
static bool s_volume_nvs_dirty = false;
static TickType_t s_volume_last_change_tick = 0;

static int audio_play_clamp_volume(int volume)
{
	if (volume < AUDIO_PLAY_VOLUME_MIN) {
		return AUDIO_PLAY_VOLUME_MIN;
	}
	if (volume > AUDIO_PLAY_VOLUME_MAX) {
		return AUDIO_PLAY_VOLUME_MAX;
	}
	return volume;
}

static esp_err_t audio_play_load_volume_from_nvs(int *volume)
{
	ESP_RETURN_ON_FALSE(volume != NULL, ESP_ERR_INVALID_ARG, TAG, "volume ptr is null");

	nvs_handle_t nvs = 0;
	esp_err_t err = nvs_open(AUDIO_PLAY_NVS_NAMESPACE, NVS_READWRITE, &nvs);
	if (err != ESP_OK) {
		return err;
	}

	uint8_t stored_volume = AUDIO_PLAY_VOLUME_DEFAULT;
	err = nvs_get_u8(nvs, AUDIO_PLAY_NVS_KEY_VOLUME, &stored_volume);
	nvs_close(nvs);

	if (err == ESP_OK) {
		*volume = audio_play_clamp_volume((int)stored_volume);
		return ESP_OK;
	}

	if (err == ESP_ERR_NVS_NOT_FOUND) {
		*volume = AUDIO_PLAY_VOLUME_DEFAULT;
		return ESP_OK;
	}

	return err;
}

static esp_err_t audio_play_save_volume_to_nvs(int volume)
{
	uint8_t stored_volume = (uint8_t)audio_play_clamp_volume(volume);
	nvs_handle_t nvs = 0;
	esp_err_t err = nvs_open(AUDIO_PLAY_NVS_NAMESPACE, NVS_READWRITE, &nvs);
	if (err != ESP_OK) {
		return err;
	}

	err = nvs_set_u8(nvs, AUDIO_PLAY_NVS_KEY_VOLUME, stored_volume);
	if (err == ESP_OK) {
		err = nvs_commit(nvs);
	}
	nvs_close(nvs);
	return err;
}

static bool audio_play_is_valid_logical_io(uint8_t logical_io)
{
	return logical_io < AUDIO_PLAY_LOGICAL_IO_NUM;
}

static bool audio_play_bitmask_to_song_number(uint8_t bit_mask, uint8_t *song_number)
{
	if (song_number == NULL || bit_mask == 0xFF) {
		return false;
	}

	uint16_t candidate = (uint16_t)(0xFFU - bit_mask);
	if (candidate == 0 || candidate > 255) {
		return false;
	}

	*song_number = (uint8_t)candidate;
	return true;
}

static void audio_play_apply_volume_delta_counts(int delta_counts)
{
#if !AUDIO_PLAY_ENCODER_CW_POSITIVE
	delta_counts = -delta_counts;
#endif

	if (delta_counts == 0) {
		return;
	}

	s_encoder_accum_counts += delta_counts;
	int volume_steps = s_encoder_accum_counts / AUDIO_PLAY_ENCODER_COUNTS_PER_STEP;
	if (volume_steps == 0) {
		return;
	}

	int target_volume = s_current_volume + volume_steps;
	if (target_volume > AUDIO_PLAY_VOLUME_MAX) {
		target_volume = AUDIO_PLAY_VOLUME_MAX;
		s_encoder_accum_counts = 0;
	} else if (target_volume < AUDIO_PLAY_VOLUME_MIN) {
		target_volume = AUDIO_PLAY_VOLUME_MIN;
		s_encoder_accum_counts = 0;
	} else {
		s_encoder_accum_counts -= volume_steps * AUDIO_PLAY_ENCODER_COUNTS_PER_STEP;
	}

	if (target_volume == s_current_volume) {
		return;
	}

	s_current_volume = target_volume;
	(void)dysv5w_set_volume((uint8_t)s_current_volume);
	s_volume_nvs_dirty = true;
	s_volume_last_change_tick = xTaskGetTickCount();
	ESP_LOGI(TAG, "encoder volume -> %d", s_current_volume);
}

static void audio_play_pcnt_monitor_task(void *arg)
{
	(void)arg;
	int pulse_count = 0;
	int prev_pulse_count = 0;

	if (s_pcnt_unit != NULL) {
		(void)pcnt_unit_get_count(s_pcnt_unit, &prev_pulse_count);
	}

	while (true) {
		if (s_pcnt_unit != NULL && pcnt_unit_get_count(s_pcnt_unit, &pulse_count) == ESP_OK) {
			int delta_counts = pulse_count - prev_pulse_count;
			if (delta_counts != 0) {
				audio_play_apply_volume_delta_counts(delta_counts);
			}
			prev_pulse_count = pulse_count;
		}

		if (s_volume_nvs_dirty) {
			TickType_t now = xTaskGetTickCount();
			if ((now - s_volume_last_change_tick) >= pdMS_TO_TICKS(AUDIO_PLAY_VOLUME_SAVE_IDLE_MS)) {
				esp_err_t save_err = audio_play_save_volume_to_nvs(s_current_volume);
				if (save_err == ESP_OK) {
					s_volume_nvs_dirty = false;
					ESP_LOGI(TAG, "volume saved to NVS -> %d", s_current_volume);
				} else {
					s_volume_last_change_tick = now;
					ESP_LOGW(TAG, "save volume to NVS failed: %s", esp_err_to_name(save_err));
				}
			}
		}

		vTaskDelay(pdMS_TO_TICKS(AUDIO_PLAY_PCNT_REPORT_PERIOD_MS));
	}
}

static esp_err_t audio_play_init_pcnt(void)
{
	if (s_pcnt_unit != NULL) {
		return ESP_OK;
	}

	pcnt_unit_config_t unit_config = {
		.high_limit = AUDIO_PLAY_PCNT_HIGH_LIMIT,
		.low_limit = AUDIO_PLAY_PCNT_LOW_LIMIT,
	};
	ESP_RETURN_ON_ERROR(pcnt_new_unit(&unit_config, &s_pcnt_unit), TAG, "pcnt new unit failed");

	pcnt_glitch_filter_config_t filter_config = {
		.max_glitch_ns = 1000,
	};
	ESP_RETURN_ON_ERROR(pcnt_unit_set_glitch_filter(s_pcnt_unit, &filter_config), TAG, "pcnt set glitch filter failed");

	pcnt_chan_config_t chan_a_config = {
		.edge_gpio_num = AUDIO_PLAY_EC11_A,
		.level_gpio_num = AUDIO_PLAY_EC11_B,
	};
	ESP_RETURN_ON_ERROR(pcnt_new_channel(s_pcnt_unit, &chan_a_config, &s_pcnt_chan_a), TAG, "pcnt new channel A failed");

	pcnt_chan_config_t chan_b_config = {
		.edge_gpio_num = AUDIO_PLAY_EC11_B,
		.level_gpio_num = AUDIO_PLAY_EC11_A,
	};
	ESP_RETURN_ON_ERROR(pcnt_new_channel(s_pcnt_unit, &chan_b_config, &s_pcnt_chan_b), TAG, "pcnt new channel B failed");

	ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(s_pcnt_chan_a,
							 PCNT_CHANNEL_EDGE_ACTION_DECREASE,
							 PCNT_CHANNEL_EDGE_ACTION_INCREASE),
			    TAG,
			    "pcnt set edge action A failed");
	ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(s_pcnt_chan_a,
							  PCNT_CHANNEL_LEVEL_ACTION_KEEP,
							  PCNT_CHANNEL_LEVEL_ACTION_INVERSE),
			    TAG,
			    "pcnt set level action A failed");

	ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(s_pcnt_chan_b,
							 PCNT_CHANNEL_EDGE_ACTION_INCREASE,
							 PCNT_CHANNEL_EDGE_ACTION_DECREASE),
			    TAG,
			    "pcnt set edge action B failed");
	ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(s_pcnt_chan_b,
							  PCNT_CHANNEL_LEVEL_ACTION_KEEP,
							  PCNT_CHANNEL_LEVEL_ACTION_INVERSE),
			    TAG,
			    "pcnt set level action B failed");

	ESP_RETURN_ON_ERROR(pcnt_unit_enable(s_pcnt_unit), TAG, "pcnt enable failed");
	ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(s_pcnt_unit), TAG, "pcnt clear count failed");
	ESP_RETURN_ON_ERROR(pcnt_unit_start(s_pcnt_unit), TAG, "pcnt start failed");

	BaseType_t task_ok = xTaskCreate(audio_play_pcnt_monitor_task,
					 "pcnt_monitor",
					 AUDIO_PLAY_PCNT_TASK_STACK,
					 NULL,
					 AUDIO_PLAY_PCNT_TASK_PRIO,
					 &s_pcnt_monitor_task_handle);
	ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_FAIL, TAG, "create pcnt monitor task failed");

	ESP_LOGI(TAG, "pcnt init done, EC11 A=%d B=%d, volume range=%d-%d", AUDIO_PLAY_EC11_A, AUDIO_PLAY_EC11_B, AUDIO_PLAY_VOLUME_MIN, AUDIO_PLAY_VOLUME_MAX);
	return ESP_OK;
}

static void audio_play_busy_monitor_task(void *arg)
{
	int previous_level = gpio_get_level(AUDIO_PLAY_BUSY_MONITOR_GPIO);

	while (true) {
		int current_level = gpio_get_level(AUDIO_PLAY_BUSY_MONITOR_GPIO);

		if ((previous_level == 0) && (current_level == 1) && (s_event_queue != NULL)) {
			audio_play_event_t evt = {
				.type = AUDIO_PLAY_EVENT_BUSY_RISING,
				.busy_level = current_level,
				.tick = xTaskGetTickCount(),
			};
			if (xQueueSend(s_event_queue, &evt, 0) != pdTRUE) {
				ESP_LOGW(TAG, "event queue full, drop BUSY rising event");
			}
		}

		previous_level = current_level;
		vTaskDelay(pdMS_TO_TICKS(AUDIO_PLAY_BUSY_MONITOR_PERIOD_MS));
	}
}

static void audio_play_button_monitor_task(void *arg)
{
	int previous_levels[AUDIO_PLAY_BUTTON_NUM] = {0};

	for (uint8_t i = 0; i < AUDIO_PLAY_BUTTON_NUM; ++i) {
		previous_levels[i] = gpio_get_level(s_button_gpio_map[i]);
	}

	while (true) {
		for (uint8_t i = 0; i < AUDIO_PLAY_BUTTON_NUM; ++i) {
			int current_level = gpio_get_level(s_button_gpio_map[i]);

			if ((previous_levels[i] == 1) && (current_level == 0) && (s_event_queue != NULL)) {
				audio_play_event_t evt = {
					.type = AUDIO_PLAY_EVENT_BUTTON_PRESSED,
					.busy_level = -1,
					.tick = xTaskGetTickCount(),
					.button_index = i + 1,
					.logical_io = i,
				};
				if (xQueueSend(s_event_queue, &evt, 0) != pdTRUE) {
					ESP_LOGW(TAG, "event queue full, drop BUTTON%u press event", (unsigned)(i + 1));
				}
			}

			previous_levels[i] = current_level;
		}

		vTaskDelay(pdMS_TO_TICKS(AUDIO_PLAY_BUTTON_MONITOR_PERIOD_MS));
	}
}

esp_err_t audio_play_set_io_level(uint8_t logical_io, uint32_t level)
{
	ESP_RETURN_ON_FALSE(s_is_initialized, ESP_ERR_INVALID_STATE, TAG, "audio_play not initialized");
	ESP_RETURN_ON_FALSE(audio_play_is_valid_logical_io(logical_io), ESP_ERR_INVALID_ARG, TAG, "invalid logical_io");
	ESP_RETURN_ON_FALSE(level <= 1, ESP_ERR_INVALID_ARG, TAG, "level must be 0 or 1");

	if (level == 0) {
		s_shadow_io_mask &= (uint8_t)(~(1U << logical_io));
	} else {
		s_shadow_io_mask |= (uint8_t)(1U << logical_io);
	}

	return ESP_OK;
}

int audio_play_get_io_level(uint8_t logical_io)
{
	if (!s_is_initialized || !audio_play_is_valid_logical_io(logical_io)) {
		return -1;
	}

	return (int)((s_shadow_io_mask >> logical_io) & 0x01U);
}

esp_err_t audio_play_set_io_mask(uint8_t bit_mask)
{
	ESP_RETURN_ON_FALSE(s_is_initialized, ESP_ERR_INVALID_STATE, TAG, "audio_play not initialized");
	s_shadow_io_mask = bit_mask;
	return ESP_OK;
}

esp_err_t audio_play_trigger_once(uint8_t logical_io, uint32_t low_time_ms)
{
	ESP_RETURN_ON_FALSE(s_is_initialized, ESP_ERR_INVALID_STATE, TAG, "audio_play not initialized");
	ESP_RETURN_ON_FALSE(audio_play_is_valid_logical_io(logical_io), ESP_ERR_INVALID_ARG, TAG, "invalid logical_io");

	uint8_t bit_mask = (uint8_t)(0xFFU & (~(1U << logical_io)));
	return audio_play_trigger_mask_once(bit_mask, low_time_ms);
}

esp_err_t audio_play_trigger_mask_once(uint8_t bit_mask, uint32_t low_time_ms)
{
	ESP_RETURN_ON_FALSE(s_is_initialized, ESP_ERR_INVALID_STATE, TAG, "audio_play not initialized");

	uint8_t song_number = 0;
	ESP_RETURN_ON_FALSE(audio_play_bitmask_to_song_number(bit_mask, &song_number),
				   ESP_ERR_INVALID_ARG,
				   TAG,
				   "invalid trigger bit_mask=0x%02X",
				   (unsigned)bit_mask);

	s_shadow_io_mask = bit_mask;
	ESP_RETURN_ON_ERROR(dysv5w_play_track(song_number), TAG, "play song %u failed", (unsigned)song_number);
	if (low_time_ms > 0) {
		vTaskDelay(pdMS_TO_TICKS(low_time_ms));
	}
	s_shadow_io_mask = 0xFF;
	return ESP_OK;
}

esp_err_t audio_play_stop_playback(void)
{
	ESP_RETURN_ON_FALSE(s_is_initialized, ESP_ERR_INVALID_STATE, TAG, "audio_play not initialized");
	ESP_RETURN_ON_ERROR(dysv5w_stop_playback(), TAG, "stop playback failed");
	s_shadow_io_mask = 0xFF;
	return ESP_OK;
}

esp_err_t audio_play_get_physical_gpio(uint8_t logical_io, gpio_num_t *gpio_num)
{
	(void)logical_io;
	(void)gpio_num;
	return ESP_ERR_NOT_SUPPORTED;
}

QueueHandle_t audio_play_get_event_queue(void)
{
	return s_event_queue;
}

esp_err_t audio_play_init(void)
{
	if (s_is_initialized) {
		return ESP_OK;
	}

	ESP_RETURN_ON_ERROR(dysv5w_init(), TAG, "init DYSV UART adapter failed");
	ESP_RETURN_ON_ERROR(audio_play_init_pcnt(), TAG, "init PCNT failed");

	gpio_config_t busy_input_cfg = {
		.pin_bit_mask = 1ULL << AUDIO_PLAY_BUSY_MONITOR_GPIO,
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,
	};
	ESP_RETURN_ON_ERROR(gpio_config(&busy_input_cfg), TAG, "config BUSY input failed");

	uint64_t button_input_mask = 0;
	for (uint8_t i = 0; i < AUDIO_PLAY_BUTTON_NUM; ++i) {
		button_input_mask |= (1ULL << s_button_gpio_map[i]);
	}

	gpio_config_t button_input_cfg = {
		.pin_bit_mask = button_input_mask,
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,
	};
	ESP_RETURN_ON_ERROR(gpio_config(&button_input_cfg), TAG, "config button input failed");

	s_shadow_io_mask = 0xFF;

	s_event_queue = xQueueCreate(AUDIO_PLAY_EVENT_QUEUE_LEN, sizeof(audio_play_event_t));
	ESP_RETURN_ON_FALSE(s_event_queue != NULL, ESP_ERR_NO_MEM, TAG, "create event queue failed");

	BaseType_t task_ok = xTaskCreate(audio_play_busy_monitor_task,
									 "busy_monitor",
									 AUDIO_PLAY_BUSY_MONITOR_TASK_STACK,
									 NULL,
									 AUDIO_PLAY_BUSY_MONITOR_TASK_PRIO,
									 &s_busy_monitor_task_handle);
	ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_FAIL, TAG, "create busy monitor task failed");

	task_ok = xTaskCreate(audio_play_button_monitor_task,
					 "button_monitor",
					 AUDIO_PLAY_BUTTON_MONITOR_TASK_STACK,
					 NULL,
					 AUDIO_PLAY_BUTTON_MONITOR_TASK_PRIO,
					 &s_button_monitor_task_handle);
	ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_FAIL, TAG, "create button monitor task failed");

	s_is_initialized = true;
	ESP_LOGI(TAG, "audio_play init done, DYSV UART mode enabled, monitor BUSY(GPIO8), BUTTON1-4(GPIO7/6/5/4)");

	int initial_volume = AUDIO_PLAY_VOLUME_DEFAULT;
	esp_err_t load_err = audio_play_load_volume_from_nvs(&initial_volume);
	if (load_err != ESP_OK) {
		ESP_LOGW(TAG, "load volume from NVS failed, use default=%d, err=%s", AUDIO_PLAY_VOLUME_DEFAULT, esp_err_to_name(load_err));
		initial_volume = AUDIO_PLAY_VOLUME_DEFAULT;
	}

	s_current_volume = audio_play_clamp_volume(initial_volume);
	s_encoder_accum_counts = 0;
	s_volume_nvs_dirty = false;
	s_volume_last_change_tick = xTaskGetTickCount();
	(void)dysv5w_set_volume((uint8_t)s_current_volume);
	ESP_LOGI(TAG, "initial volume=%d", s_current_volume);

	return ESP_OK;
}
