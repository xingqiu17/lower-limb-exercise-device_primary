#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t dysv_uart_adapter_init(void);
esp_err_t dysv_uart_adapter_play_track(uint8_t song_number);
int dysv_uart_adapter_get_playback_state(void);

#ifdef __cplusplus
}
#endif
