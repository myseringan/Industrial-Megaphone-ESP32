#include "http_server.h"
#include "audio_storage.h"
#include "audio_player.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_heap_caps.h"
#include <string.h>

#define TAG "HTTP"

static httpd_handle_t server = NULL;
static volatile bool client_connected = false;
static TimerHandle_t connection_timer = NULL;

/* LED control */
static void led_set(bool on)
{
    gpio_set_level(CONFIG_LED_GPIO, on ? 1 : 0);
}

/* Connection timeout callback */
static void connection_timeout_cb(TimerHandle_t timer)
{
    client_connected = false;
    led_set(false);
    ESP_LOGW(TAG, "Client connection timeout - LED OFF");
}

/* Reset connection timer */
static void reset_connection_timer(void)
{
    if (!client_connected) {
        client_connected = true;
        led_set(true);
        ESP_LOGI(TAG, "Client connected - LED ON");
    }
    
    if (connection_timer) {
        xTimerReset(connection_timer, 0);
    }
}

bool http_server_client_connected(void)
{
    return client_connected;
}

/* 
 * GET /health
 * Response: {"megaphone_version": "1.0.0"}
 */
static esp_err_t health_handler(httpd_req_t *req)
{
    reset_connection_timer();
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "megaphone_version", CONFIG_APP_VERSION);
    
    char *json = cJSON_PrintUnformatted(root);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    
    free(json);
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "GET /health");
    return ESP_OK;
}

/*
 * POST /update-audio
 * Content-Type: application/octet-stream
 * Headers: X-Message-Text, X-Audio-Hash
 * Body: raw audio bytes (MP3)
 */
static esp_err_t update_audio_handler(httpd_req_t *req)
{
    reset_connection_timer();
    
    ESP_LOGI(TAG, "POST /update-audio (content_len=%d)", req->content_len);
    
    if (req->content_len == 0 || req->content_len > 2 * 1024 * 1024) {  // Max 2MB
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }
    
    // Get message_text and audio_hash from headers
    char message_text[512] = {0};
    char audio_hash[128] = {0};
    
    if (httpd_req_get_hdr_value_str(req, "X-Message-Text", message_text, sizeof(message_text)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing X-Message-Text header");
        return ESP_FAIL;
    }
    
    if (httpd_req_get_hdr_value_str(req, "X-Audio-Hash", audio_hash, sizeof(audio_hash)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing X-Audio-Hash header");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Receiving audio: text='%s', hash='%s'", message_text, audio_hash);
    
    // Allocate buffer (prefer PSRAM)
    uint8_t *audio_data = heap_caps_malloc(req->content_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!audio_data) {
        audio_data = malloc(req->content_len);
    }
    if (!audio_data) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    
    // Receive binary data
    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, (char*)(audio_data + received), req->content_len - received);
        if (ret <= 0) {
            free(audio_data);
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Receive timeout");
            } else {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            }
            return ESP_FAIL;
        }
        received += ret;
        
        if (received % 10000 == 0) {
            ESP_LOGD(TAG, "Received %d/%d bytes", received, req->content_len);
        }
    }
    
    // Store track
    esp_err_t ret = audio_storage_update(message_text, audio_hash, audio_data, received);
    free(audio_data);
    
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage failed");
        return ESP_FAIL;
    }
    
    // Success - return 200 OK
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_send(req, NULL, 0);
    
    ESP_LOGI(TAG, "Stored: '%s' (%d bytes)", message_text, received);
    return ESP_OK;
}

/*
 * POST /play-message
 * Content-Type: application/json
 * Body: {"message_text": "...", "audio_hash": "..."}
 */
static esp_err_t play_message_handler(httpd_req_t *req)
{
    reset_connection_timer();
    
    ESP_LOGI(TAG, "POST /play-message");
    
    if (req->content_len == 0 || req->content_len > 2048) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    
    char *buf = malloc(req->content_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    
    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, buf + received, req->content_len - received);
        if (ret <= 0) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[received] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *text_item = cJSON_GetObjectItem(root, "message_text");
    cJSON *hash_item = cJSON_GetObjectItem(root, "audio_hash");
    
    if (!text_item || !hash_item ||
        !cJSON_IsString(text_item) || !cJSON_IsString(hash_item)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing message_text or audio_hash");
        return ESP_FAIL;
    }
    
    const char *message_text = text_item->valuestring;
    const char *audio_hash = hash_item->valuestring;
    
    ESP_LOGI(TAG, "Play request: text='%s', hash='%s'", message_text, audio_hash);
    
    // Get audio track
    uint8_t *audio_data = NULL;
    size_t audio_len = 0;
    
    esp_err_t ret = audio_storage_get(message_text, audio_hash, &audio_data, &audio_len);
    cJSON_Delete(root);
    
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Track not found: '%s'", message_text);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found - no cached audio for request hash");
        return ESP_FAIL;
    }
    
    // Play audio
    ret = audio_player_play(audio_data, audio_len);
    free(audio_data);  // audio_storage_get allocates new buffer
    
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Playback failed");
        return ESP_FAIL;
    }
    
    // Success - return 200 OK
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_send(req, NULL, 0);
    
    ESP_LOGI(TAG, "Playing: '%s' (%d bytes)", message_text, audio_len);
    return ESP_OK;
}

/*
 * POST /check-audio (extra endpoint for efficiency)
 * Body: {"message_text": "...", "audio_hash": "..."}
 * Response: {"exists": true/false}
 */
static esp_err_t check_audio_handler(httpd_req_t *req)
{
    reset_connection_timer();
    
    if (req->content_len == 0 || req->content_len > 1024) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    
    char *buf = malloc(req->content_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    
    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, buf + received, req->content_len - received);
        if (ret <= 0) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[received] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *text_item = cJSON_GetObjectItem(root, "message_text");
    cJSON *hash_item = cJSON_GetObjectItem(root, "audio_hash");
    
    if (!text_item || !hash_item ||
        !cJSON_IsString(text_item) || !cJSON_IsString(hash_item)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing message_text or audio_hash");
        return ESP_FAIL;
    }
    
    bool exists = audio_storage_exists(text_item->valuestring, hash_item->valuestring);
    
    ESP_LOGI(TAG, "Check: '%s' hash='%s' -> %s", 
             text_item->valuestring, hash_item->valuestring, 
             exists ? "EXISTS" : "NOT FOUND");
    
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    if (exists) {
        httpd_resp_sendstr(req, "{\"exists\":true}");
    } else {
        httpd_resp_sendstr(req, "{\"exists\":false}");
    }
    
    return ESP_OK;
}

/* URI handlers */
static const httpd_uri_t uri_health = {
    .uri       = "/health",
    .method    = HTTP_GET,
    .handler   = health_handler,
};

static const httpd_uri_t uri_update_audio = {
    .uri       = "/update-audio",
    .method    = HTTP_POST,
    .handler   = update_audio_handler,
};

static const httpd_uri_t uri_play_message = {
    .uri       = "/play-message",
    .method    = HTTP_POST,
    .handler   = play_message_handler,
};

static const httpd_uri_t uri_check_audio = {
    .uri       = "/check-audio",
    .method    = HTTP_POST,
    .handler   = check_audio_handler,
};

esp_err_t http_server_start(void)
{
    /* Disable httpd warning/error spam from bad requests */
    esp_log_level_set("httpd_parse", ESP_LOG_NONE);
    esp_log_level_set("httpd_txrx", ESP_LOG_NONE);
    esp_log_level_set("httpd_sess", ESP_LOG_NONE);
    
    /* Initialize LED */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);
    led_set(false);
    
    /* Create connection timeout timer (30 seconds) */
    connection_timer = xTimerCreate("conn_timer", pdMS_TO_TICKS(30000), 
                                     pdFALSE, NULL, connection_timeout_cb);
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CONFIG_SERVER_PORT;
    config.max_uri_handlers = 8;
    config.stack_size = 16384;
    config.recv_wait_timeout = 30;
    config.send_wait_timeout = 30;
    
    ESP_LOGI(TAG, "Starting server on port %d", config.server_port);
    
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server");
        return ESP_FAIL;
    }
    
    httpd_register_uri_handler(server, &uri_health);
    httpd_register_uri_handler(server, &uri_update_audio);
    httpd_register_uri_handler(server, &uri_play_message);
    httpd_register_uri_handler(server, &uri_check_audio);
    
    ESP_LOGI(TAG, "Server started");
    return ESP_OK;
}

esp_err_t http_server_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
    return ESP_OK;
}
