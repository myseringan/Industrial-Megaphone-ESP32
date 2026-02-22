/*
 * ESP32-P4-WIFI6-DEV-KIT Audio Server (Megaphone Player)
 * 
 * Connection: Ethernet (RJ-45)
 * Storage: Flash (LittleFS) - persistent!
 * 
 * HTTP API (swagger compatible):
 *   GET  /health        - {"megaphone_version": "1.0.0"}
 *   POST /update-audio  - upload audio (binary), headers: X-Message-Text, X-Audio-Hash
 *   POST /play-message  - play stored {message_text, audio_hash}
 *   POST /check-audio   - check if exists {message_text, audio_hash} -> {exists: bool}
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "ethernet.h"
#include "http_server.h"
#include "audio_player.h"
#include "audio_storage.h"

#define TAG "MAIN"

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Megaphone Player v%s", CONFIG_APP_VERSION);
    ESP_LOGI(TAG, "Connection: Ethernet (RJ-45)");
    ESP_LOGI(TAG, "Storage: Flash (LittleFS)");
    ESP_LOGI(TAG, "========================================");
    
    /* Initialize audio storage (LittleFS) */
    ESP_ERROR_CHECK(audio_storage_init());
    
    /* Initialize audio player */
    ESP_ERROR_CHECK(audio_player_init());
    
    /* Connect via Ethernet */
    ESP_LOGI(TAG, "Initializing Ethernet...");
    esp_err_t ret = ethernet_init();
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet connection failed! Restarting...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }
    
    /* Start HTTP server */
    ESP_ERROR_CHECK(http_server_start());
    
    /* Print connection info */
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Server ready!");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "  IP Address: %s", ethernet_get_ip());
    ESP_LOGI(TAG, "  Port: %d", CONFIG_SERVER_PORT);
    ESP_LOGI(TAG, "  Stored tracks: %d", audio_storage_count());
    ESP_LOGI(TAG, "  Free space: %d KB", audio_storage_free_kb());
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "API:");
    ESP_LOGI(TAG, "  GET  /health");
    ESP_LOGI(TAG, "  POST /update-audio  (binary + headers)");
    ESP_LOGI(TAG, "  POST /play-message  (JSON)");
    ESP_LOGI(TAG, "  POST /check-audio   (JSON)");
    ESP_LOGI(TAG, "========================================");
    
    /* Main loop - log status */
    while (1) {
        ESP_LOGI(TAG, "Status: Eth=%s, Client=%s, Tracks=%d, Free=%dKB",
                 ethernet_is_connected() ? "OK" : "NO",
                 http_server_client_connected() ? "YES" : "wait",
                 audio_storage_count(),
                 audio_storage_free_kb());
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
