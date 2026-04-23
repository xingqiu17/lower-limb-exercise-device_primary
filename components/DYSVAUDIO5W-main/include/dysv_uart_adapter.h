#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t dysv5w_init(void);
esp_err_t dysv5w_play_track(uint8_t song_number);
esp_err_t dysv5w_pause_playback(void);
esp_err_t dysv5w_resume_playback(void);
esp_err_t dysv5w_stop_playback(void);
int dysv5w_get_playback_state(void);
int dysv5w_set_volume(uint8_t volume);
int dysv5w_volumeIncrement(void);
int dysv5w_volumeDecrement(void);


#ifdef __cplusplus
}
#endif
