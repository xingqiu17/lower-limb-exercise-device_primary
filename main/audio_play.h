#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_PLAY_LOGICAL_IO_NUM 8

typedef enum {
	AUDIO_PLAY_EVENT_BUSY_RISING = 0,
} audio_play_event_type_t;

typedef struct {
	audio_play_event_type_t type;
	int busy_level;
	uint32_t tick;
} audio_play_event_t;

esp_err_t audio_play_init(void);

QueueHandle_t audio_play_get_event_queue(void);

esp_err_t audio_play_set_io_level(uint8_t logical_io, uint32_t level);

int audio_play_get_io_level(uint8_t logical_io);

esp_err_t audio_play_set_io_mask(uint8_t bit_mask);

esp_err_t audio_play_trigger_once(uint8_t logical_io, uint32_t low_time_ms);

esp_err_t audio_play_get_physical_gpio(uint8_t logical_io, gpio_num_t *gpio_num);

#ifdef __cplusplus
}
#endif
