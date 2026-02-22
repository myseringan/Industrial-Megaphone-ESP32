#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * Start HTTP server
 */
esp_err_t http_server_start(void);

/**
 * Stop HTTP server
 */
esp_err_t http_server_stop(void);

/**
 * Check if client is connected (has sent request recently)
 */
bool http_server_client_connected(void);
