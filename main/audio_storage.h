#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * Initialize audio storage (LittleFS)
 */
esp_err_t audio_storage_init(void);

/**
 * Store or update audio track on flash
 * @param text - phrase text (key)
 * @param hash - hash of audio data (version check)
 * @param audio_data - raw PCM audio bytes
 * @param audio_len - length of audio data
 */
esp_err_t audio_storage_update(const char *text, const char *hash, 
                                const uint8_t *audio_data, size_t audio_len);

/**
 * Get audio track by text
 * @param text - phrase text
 * @param expected_hash - expected hash (for validation)
 * @param out_audio - pointer to receive audio data (caller must free!)
 * @param out_len - pointer to receive audio length
 * @return ESP_OK if found and hash matches, ESP_ERR_NOT_FOUND otherwise
 */
esp_err_t audio_storage_get(const char *text, const char *expected_hash,
                            uint8_t **out_audio, size_t *out_len);

/**
 * Check if track exists with matching hash
 */
bool audio_storage_exists(const char *text, const char *hash);

/**
 * Get number of stored tracks
 */
int audio_storage_count(void);

/**
 * Delete a track
 */
esp_err_t audio_storage_delete(const char *text);

/**
 * Get free storage space in KB
 */
int audio_storage_free_kb(void);
