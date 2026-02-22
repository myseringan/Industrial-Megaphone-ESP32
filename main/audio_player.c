#include "audio_player.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "es8311.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "PLAYER"

/* ESP32-P4-WIFI6-DEV-KIT pins */
#define I2C_SDA     7
#define I2C_SCL     8
#define I2S_MCLK    13
#define I2S_BCLK    12
#define I2S_WS      10
#define I2S_DOUT    9
#define I2S_DIN     11
#define PA_PIN      53

#define I2C_PORT    I2C_NUM_0
#define SAMPLE_RATE 44100

static i2s_chan_handle_t tx_handle = NULL;
static es8311_handle_t es8311_handle = NULL;
static SemaphoreHandle_t play_mutex = NULL;
static volatile bool is_playing = false;

static esp_err_t init_i2c(void)
{
    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &cfg));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));
    
    ESP_LOGI(TAG, "I2C initialized");
    return ESP_OK;
}

static esp_err_t init_i2s(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));
    
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK,
            .bclk = I2S_BCLK,
            .ws = I2S_WS,
            .dout = I2S_DOUT,
            .din = I2S_DIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    
    ESP_LOGI(TAG, "I2S initialized at %d Hz", SAMPLE_RATE);
    return ESP_OK;
}

static esp_err_t init_codec(void)
{
    es8311_handle = es8311_create(I2C_PORT, ES8311_ADDRRES_0);
    if (!es8311_handle) {
        ESP_LOGE(TAG, "ES8311 create failed");
        return ESP_FAIL;
    }
    
    es8311_clock_config_t clk_cfg = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = SAMPLE_RATE * 256,
        .sample_frequency = SAMPLE_RATE,
    };
    
    ESP_ERROR_CHECK(es8311_init(es8311_handle, &clk_cfg, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16));
    es8311_voice_volume_set(es8311_handle, CONFIG_AUDIO_VOLUME, NULL);
    es8311_microphone_config(es8311_handle, false);
    
    ESP_LOGI(TAG, "ES8311 initialized (volume=%d)", CONFIG_AUDIO_VOLUME);
    return ESP_OK;
}

static void init_pa(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PA_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);
    gpio_set_level(PA_PIN, 1);
    ESP_LOGI(TAG, "PA enabled");
}

esp_err_t audio_player_init(void)
{
    play_mutex = xSemaphoreCreateMutex();
    if (!play_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_FAIL;
    }
    
    ESP_ERROR_CHECK(init_i2c());
    ESP_ERROR_CHECK(init_i2s());
    ESP_ERROR_CHECK(init_codec());
    init_pa();
    
    ESP_LOGI(TAG, "Audio player ready (44100 Hz, 16-bit, stereo)");
    return ESP_OK;
}

esp_err_t audio_player_play(const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(play_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Player busy");
        return ESP_ERR_INVALID_STATE;
    }
    
    is_playing = true;
    ESP_LOGI(TAG, "Playing %d bytes PCM", len);
    
    size_t bytes_written;
    size_t offset = 0;
    const size_t chunk_size = 4096;
    
    while (offset < len && is_playing) {
        size_t to_write = (len - offset > chunk_size) ? chunk_size : (len - offset);
        esp_err_t ret = i2s_channel_write(tx_handle, data + offset, to_write, &bytes_written, pdMS_TO_TICKS(1000));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2S write error: %s", esp_err_to_name(ret));
            break;
        }
        offset += bytes_written;
    }
    
    is_playing = false;
    xSemaphoreGive(play_mutex);
    
    ESP_LOGI(TAG, "Playback complete (%d bytes)", offset);
    return ESP_OK;
}

esp_err_t audio_player_stop(void)
{
    is_playing = false;
    return ESP_OK;
}

esp_err_t audio_player_set_volume(int volume)
{
    if (es8311_handle) {
        es8311_voice_volume_set(es8311_handle, volume, NULL);
        ESP_LOGI(TAG, "Volume set to %d", volume);
    }
    return ESP_OK;
}

bool audio_player_is_playing(void)
{
    return is_playing;
}
