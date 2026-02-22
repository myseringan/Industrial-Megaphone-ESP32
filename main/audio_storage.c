#include "audio_storage.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#define TAG "STORAGE"
#define STORAGE_PATH "/littlefs"
#define MAX_FILENAME 128

static bool storage_mounted = false;

/* Initialize LittleFS */
esp_err_t audio_storage_init(void)
{
    ESP_LOGI(TAG, "Initializing LittleFS storage...");
    
    esp_vfs_littlefs_conf_t conf = {
        .base_path = STORAGE_PATH,
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    
    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Partition 'storage' not found! Add to partitions.csv");
        } else {
            ESP_LOGE(TAG, "Failed to mount LittleFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }
    
    size_t total = 0, used = 0;
    esp_littlefs_info("storage", &total, &used);
    ESP_LOGI(TAG, "LittleFS mounted: %d KB total, %d KB used, %d KB free", 
             total/1024, used/1024, (total-used)/1024);
    
    storage_mounted = true;
    return ESP_OK;
}

/* Generate filename from text (sanitize) */
static void make_filename(const char *text, char *filename, size_t len)
{
    snprintf(filename, len, "%s/", STORAGE_PATH);
    size_t prefix_len = strlen(filename);
    size_t i = 0;
    
    // Sanitize: only alphanumeric and underscore, max 32 chars
    while (text[i] && i < 32 && (prefix_len + i) < (len - 10)) {
        char c = text[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
            (c >= '0' && c <= '9') || c == '_') {
            filename[prefix_len + i] = c;
        } else {
            filename[prefix_len + i] = '_';
        }
        i++;
    }
    filename[prefix_len + i] = '\0';
    strcat(filename, ".pcm");
}

/* Generate hash filename */
static void make_hash_filename(const char *text, char *filename, size_t len)
{
    snprintf(filename, len, "%s/", STORAGE_PATH);
    size_t prefix_len = strlen(filename);
    size_t i = 0;
    
    while (text[i] && i < 32 && (prefix_len + i) < (len - 10)) {
        char c = text[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
            (c >= '0' && c <= '9') || c == '_') {
            filename[prefix_len + i] = c;
        } else {
            filename[prefix_len + i] = '_';
        }
        i++;
    }
    filename[prefix_len + i] = '\0';
    strcat(filename, ".hash");
}

/* Check if track exists with matching hash */
bool audio_storage_exists(const char *text, const char *hash)
{
    if (!storage_mounted || !text || !hash) return false;
    
    char hash_file[MAX_FILENAME];
    make_hash_filename(text, hash_file, sizeof(hash_file));
    
    FILE *f = fopen(hash_file, "r");
    if (!f) {
        ESP_LOGD(TAG, "Hash file not found: %s", hash_file);
        return false;
    }
    
    char stored_hash[128] = {0};
    fgets(stored_hash, sizeof(stored_hash), f);
    fclose(f);
    
    // Remove newline if present
    char *nl = strchr(stored_hash, '\n');
    if (nl) *nl = '\0';
    nl = strchr(stored_hash, '\r');
    if (nl) *nl = '\0';
    
    bool match = (strcmp(stored_hash, hash) == 0);
    ESP_LOGI(TAG, "Hash check '%s': stored='%s', requested='%s', match=%d", 
             text, stored_hash, hash, match);
    
    return match;
}

/* Store audio track */
esp_err_t audio_storage_update(const char *text, const char *hash,
                                const uint8_t *audio, size_t len)
{
    if (!storage_mounted) {
        ESP_LOGE(TAG, "Storage not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!text || !hash || !audio || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char pcm_file[MAX_FILENAME];
    char hash_file[MAX_FILENAME];
    make_filename(text, pcm_file, sizeof(pcm_file));
    make_hash_filename(text, hash_file, sizeof(hash_file));
    
    ESP_LOGI(TAG, "Storing '%s' (%d bytes) -> %s", text, len, pcm_file);
    
    // Write audio data
    FILE *f = fopen(pcm_file, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", pcm_file);
        return ESP_FAIL;
    }
    
    size_t written = fwrite(audio, 1, len, f);
    fclose(f);
    
    if (written != len) {
        ESP_LOGE(TAG, "Write failed: %d/%d bytes", written, len);
        remove(pcm_file);
        return ESP_FAIL;
    }
    
    // Write hash
    f = fopen(hash_file, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to write hash file");
        remove(pcm_file);
        return ESP_FAIL;
    }
    fprintf(f, "%s", hash);
    fclose(f);
    
    // Log free space
    size_t total = 0, used = 0;
    esp_littlefs_info("storage", &total, &used);
    ESP_LOGI(TAG, "Stored track '%s' (hash='%s'), free space: %d KB", 
             text, hash, (total-used)/1024);
    
    return ESP_OK;
}

/* Get audio track */
esp_err_t audio_storage_get(const char *text, const char *hash,
                             uint8_t **audio_out, size_t *len_out)
{
    if (!storage_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!text || !hash || !audio_out || !len_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // First check hash
    if (!audio_storage_exists(text, hash)) {
        ESP_LOGW(TAG, "Track '%s' not found or hash mismatch", text);
        return ESP_ERR_NOT_FOUND;
    }
    
    char pcm_file[MAX_FILENAME];
    make_filename(text, pcm_file, sizeof(pcm_file));
    
    // Get file size
    struct stat st;
    if (stat(pcm_file, &st) != 0) {
        ESP_LOGE(TAG, "Cannot stat %s", pcm_file);
        return ESP_ERR_NOT_FOUND;
    }
    
    size_t file_size = st.st_size;
    ESP_LOGI(TAG, "Loading '%s' (%d bytes) from flash", text, file_size);
    
    // Allocate buffer (prefer PSRAM)
    uint8_t *buffer = heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        buffer = malloc(file_size);
    }
    if (!buffer) {
        ESP_LOGE(TAG, "No memory for %d bytes", file_size);
        return ESP_ERR_NO_MEM;
    }
    
    // Read file
    FILE *f = fopen(pcm_file, "rb");
    if (!f) {
        free(buffer);
        return ESP_ERR_NOT_FOUND;
    }
    
    size_t read_bytes = fread(buffer, 1, file_size, f);
    fclose(f);
    
    if (read_bytes != file_size) {
        free(buffer);
        ESP_LOGE(TAG, "Read error: %d/%d", read_bytes, file_size);
        return ESP_FAIL;
    }
    
    *audio_out = buffer;
    *len_out = file_size;
    return ESP_OK;
}

/* Count stored tracks */
int audio_storage_count(void)
{
    if (!storage_mounted) return 0;
    
    int count = 0;
    DIR *dir = opendir(STORAGE_PATH);
    if (!dir) return 0;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".pcm")) {
            count++;
        }
    }
    closedir(dir);
    
    return count;
}

/* Delete a track */
esp_err_t audio_storage_delete(const char *text)
{
    if (!storage_mounted) return ESP_ERR_INVALID_STATE;
    
    char pcm_file[MAX_FILENAME];
    char hash_file[MAX_FILENAME];
    make_filename(text, pcm_file, sizeof(pcm_file));
    make_hash_filename(text, hash_file, sizeof(hash_file));
    
    remove(pcm_file);
    remove(hash_file);
    
    ESP_LOGI(TAG, "Deleted track '%s'", text);
    return ESP_OK;
}

/* Get free space in KB */
int audio_storage_free_kb(void)
{
    if (!storage_mounted) return 0;
    
    size_t total = 0, used = 0;
    esp_littlefs_info("storage", &total, &used);
    return (total - used) / 1024;
}
