#include "audio_play.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"

#define AUDIO_PLAY_BUSY_MONITOR_GPIO          GPIO_NUM_8
#define AUDIO_PLAY_EVENT_QUEUE_LEN            8
#define AUDIO_PLAY_BUSY_MONITOR_TASK_STACK    2048
#define AUDIO_PLAY_BUSY_MONITOR_TASK_PRIO     4
#define AUDIO_PLAY_BUSY_MONITOR_PERIOD_MS     10
#define AUDIO_PLAY_BUTTON_MONITOR_TASK_STACK  2048
#define AUDIO_PLAY_BUTTON_MONITOR_TASK_PRIO   4
#define AUDIO_PLAY_BUTTON_MONITOR_PERIOD_MS   10
#define AUDIO_PLAY_TRIGGER_PREPARE_MS         2

static const char *TAG = "audio_play";

static const gpio_num_t s_output_gpio_map[AUDIO_PLAY_LOGICAL_IO_NUM] = {
	GPIO_NUM_14,
	GPIO_NUM_13,
	GPIO_NUM_12,
	GPIO_NUM_11,
	GPIO_NUM_10,
	GPIO_NUM_9,
	GPIO_NUM_46,
	GPIO_NUM_3,
};

static const gpio_num_t s_button_gpio_map[AUDIO_PLAY_BUTTON_NUM] = {
	GPIO_NUM_7,
	GPIO_NUM_6,
	GPIO_NUM_5,
	GPIO_NUM_4,
};

static QueueHandle_t s_event_queue = NULL;
static TaskHandle_t s_busy_monitor_task_handle = NULL;
static TaskHandle_t s_button_monitor_task_handle = NULL;
static bool s_is_initialized = false;

static bool audio_play_is_valid_logical_io(uint8_t logical_io)
{
	return logical_io < AUDIO_PLAY_LOGICAL_IO_NUM;
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

	return gpio_set_level(s_output_gpio_map[logical_io], (int)level);
}

int audio_play_get_io_level(uint8_t logical_io)
{
	if (!s_is_initialized || !audio_play_is_valid_logical_io(logical_io)) {
		return -1;
	}

	return gpio_get_level(s_output_gpio_map[logical_io]);
}

esp_err_t audio_play_set_io_mask(uint8_t bit_mask)
{
	ESP_RETURN_ON_FALSE(s_is_initialized, ESP_ERR_INVALID_STATE, TAG, "audio_play not initialized");

	for (uint8_t i = 0; i < AUDIO_PLAY_LOGICAL_IO_NUM; ++i) {
		uint32_t level = (bit_mask >> i) & 0x01;
		esp_err_t ret = gpio_set_level(s_output_gpio_map[i], (int)level);
		if (ret != ESP_OK) {
			return ret;
		}
	}

	return ESP_OK;
}

esp_err_t audio_play_trigger_once(uint8_t logical_io, uint32_t low_time_ms)
{
	ESP_RETURN_ON_FALSE(s_is_initialized, ESP_ERR_INVALID_STATE, TAG, "audio_play not initialized");
	ESP_RETURN_ON_FALSE(audio_play_is_valid_logical_io(logical_io), ESP_ERR_INVALID_ARG, TAG, "invalid logical_io");

	gpio_num_t gpio = s_output_gpio_map[logical_io];

	ESP_RETURN_ON_ERROR(gpio_set_level(gpio, 1), TAG, "set high before trigger failed");
	vTaskDelay(pdMS_TO_TICKS(AUDIO_PLAY_TRIGGER_PREPARE_MS));

	ESP_RETURN_ON_ERROR(gpio_set_level(gpio, 0), TAG, "set low trigger failed");
	vTaskDelay(pdMS_TO_TICKS(low_time_ms));

	ESP_RETURN_ON_ERROR(gpio_set_level(gpio, 1), TAG, "restore high failed");
	return ESP_OK;
}

esp_err_t audio_play_get_physical_gpio(uint8_t logical_io, gpio_num_t *gpio_num)
{
	ESP_RETURN_ON_FALSE(gpio_num != NULL, ESP_ERR_INVALID_ARG, TAG, "gpio_num is null");
	ESP_RETURN_ON_FALSE(audio_play_is_valid_logical_io(logical_io), ESP_ERR_INVALID_ARG, TAG, "invalid logical_io");

	*gpio_num = s_output_gpio_map[logical_io];
	return ESP_OK;
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

	uint64_t output_mask = 0;
	for (uint8_t i = 0; i < AUDIO_PLAY_LOGICAL_IO_NUM; ++i) {
		output_mask |= (1ULL << s_output_gpio_map[i]);
	}

	gpio_config_t output_cfg = {
		.pin_bit_mask = output_mask,
		.mode = GPIO_MODE_OUTPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,
	};
	ESP_RETURN_ON_ERROR(gpio_config(&output_cfg), TAG, "config output gpio failed");

	for (uint8_t i = 0; i < AUDIO_PLAY_LOGICAL_IO_NUM; ++i) {
		ESP_RETURN_ON_ERROR(gpio_set_level(s_output_gpio_map[i], 1), TAG, "set default level failed");
	}

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
	ESP_LOGI(TAG, "audio_play init done, monitor BUSY(GPIO8), BUTTON1-4(GPIO7/6/5/4), map IO0-7 to GPIO 14/13/12/11/10/9/46/3");

	return ESP_OK;
}
