#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * Initialize Ethernet and connect
 */
esp_err_t ethernet_init(void);

/**
 * Get current IP address as string
 */
const char* ethernet_get_ip(void);

/**
 * Check if connected
 */
bool ethernet_is_connected(void);
