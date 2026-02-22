#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * Initialize audio player (I2S + ES8311 codec)
 */
esp_err_t audio_player_init(void);

/**
 * Play raw PCM audio data (16-bit, stereo, 44100 Hz)
 * @param data - raw PCM bytes
 * @param len - length in bytes
 */
esp_err_t audio_player_play(const uint8_t *data, size_t len);

/**
 * Stop current playback
 */
esp_err_t audio_player_stop(void);

/**
 * Set volume (0-100)
 */
esp_err_t audio_player_set_volume(int volume);

/**
 * Check if currently playing
 */
bool audio_player_is_playing(void);
