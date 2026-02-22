#include "ethernet.h"
#include "esp_eth.h"
#include "esp_eth_phy.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

#define TAG "ETH"

#define ETH_CONNECTED_BIT BIT0

/* ESP32-P4-WIFI6-DEV-KIT Ethernet pins (IP101GRI PHY) */
#define ETH_MDC_GPIO        31
#define ETH_MDIO_GPIO       52
#define ETH_PHY_RST_GPIO    51
#define ETH_PHY_ADDR        1

static EventGroupHandle_t eth_event_group;
static bool connected = false;
static char ip_str[16] = "0.0.0.0";
static esp_netif_t *eth_netif = NULL;

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    switch (event_id) {
        case ETHERNET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Ethernet Link Up");
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Ethernet Link Down");
            connected = false;
            break;
        case ETHERNET_EVENT_START:
            ESP_LOGI(TAG, "Ethernet Started");
            break;
        case ETHERNET_EVENT_STOP:
            ESP_LOGI(TAG, "Ethernet Stopped");
            connected = false;
            break;
        default:
            break;
    }
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                  int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));
    ESP_LOGI(TAG, "Got IP: %s", ip_str);
    connected = true;
    xEventGroupSetBits(eth_event_group, ETH_CONNECTED_BIT);
}

esp_err_t ethernet_init(void)
{
    eth_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create default Ethernet netif
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    eth_netif = esp_netif_new(&cfg);
    
#if !CONFIG_USE_DHCP
    // Static IP configuration
    esp_netif_dhcpc_stop(eth_netif);
    
    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(ip_info));
    
    ip_info.ip.addr = esp_ip4addr_aton(CONFIG_STATIC_IP);
    ip_info.gw.addr = esp_ip4addr_aton(CONFIG_STATIC_GATEWAY);
    ip_info.netmask.addr = esp_ip4addr_aton(CONFIG_STATIC_NETMASK);
    
    esp_netif_set_ip_info(eth_netif, &ip_info);
    ESP_LOGI(TAG, "Using static IP: %s", CONFIG_STATIC_IP);
#else
    ESP_LOGI(TAG, "Using DHCP");
#endif
    
    // Init MAC and PHY configs to default
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    
    // Update PHY config based on board configuration
    phy_config.phy_addr = ETH_PHY_ADDR;
    phy_config.reset_gpio_num = ETH_PHY_RST_GPIO;
    
    // Init vendor specific MAC config
    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    esp32_emac_config.smi_gpio.mdc_num = ETH_MDC_GPIO;
    esp32_emac_config.smi_gpio.mdio_num = ETH_MDIO_GPIO;
    
    // Create new ESP32 Ethernet MAC instance
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
    
    // Create new PHY instance (IP101)
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);
    
    // Create Ethernet driver
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));
    
    // Attach Ethernet driver to netif
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));
    
    // Start Ethernet
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));
    
    ESP_LOGI(TAG, "Waiting for Ethernet connection...");
    
    // Wait for IP (timeout 30 sec)
    EventBits_t bits = xEventGroupWaitBits(eth_event_group, ETH_CONNECTED_BIT,
                                            pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
    
    if (bits & ETH_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Ethernet connected!");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Ethernet connection timeout - continuing anyway");
        return ESP_OK;  // Continue anyway, link may come up later
    }
}

const char* ethernet_get_ip(void)
{
    return ip_str;
}

bool ethernet_is_connected(void)
{
    return connected;
}
