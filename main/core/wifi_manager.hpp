/**
 * @file wifi_manager.hpp
 * @brief WiFi connection management
 */
#pragma once

#ifdef ESP_PLATFORM

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <cstring>

namespace core {

class WiFiManager {
public:
    static constexpr int CONNECTED_BIT = BIT0;
    static constexpr int FAIL_BIT = BIT1;
    
    bool init() {
        // Initialize NVS
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            ret = nvs_flash_init();
        }
        if (ret != ESP_OK) return false;
        
        // Initialize TCP/IP and WiFi
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        netif_ = esp_netif_create_default_wifi_sta();
        
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        
        // Create event group
        event_group_ = xEventGroupCreate();
        
        // Register event handlers
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
            &wifi_event_handler, this, &wifi_handler_);
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
            &wifi_event_handler, this, &ip_handler_);
        
        return true;
    }
    
    bool connect(const char* ssid, const char* password, uint32_t timeout_ms = 15000) {
        wifi_config_t wifi_config = {};
        strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        
        ESP_LOGI("WiFi", "Connecting to %s...", ssid);
        
        EventBits_t bits = xEventGroupWaitBits(event_group_,
            CONNECTED_BIT | FAIL_BIT, pdFALSE, pdFALSE,
            pdMS_TO_TICKS(timeout_ms));
        
        if (bits & CONNECTED_BIT) {
            esp_netif_ip_info_t ip_info;
            esp_netif_get_ip_info(netif_, &ip_info);
            snprintf(ip_address_, sizeof(ip_address_), IPSTR, IP2STR(&ip_info.ip));
            
            uint8_t mac[6];
            esp_wifi_get_mac(WIFI_IF_STA, mac);
            snprintf(mac_address_, sizeof(mac_address_), 
                "%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            
            ESP_LOGI("WiFi", "Connected! IP: %s", ip_address_);
            return true;
        }
        
        ESP_LOGE("WiFi", "Connection failed");
        return false;
    }
    
    bool start_mdns(const char* hostname) {
        if (mdns_init() != ESP_OK) return false;
        mdns_hostname_set(hostname);
        mdns_instance_name_set("ESP32 Camera");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
        strncpy(hostname_, hostname, sizeof(hostname_));
        ESP_LOGI("WiFi", "mDNS: %s.local", hostname);
        return true;
    }
    
    const char* ip_address() const { return ip_address_; }
    const char* mac_address() const { return mac_address_; }
    const char* hostname() const { return hostname_; }

private:
    static void wifi_event_handler(void* arg, esp_event_base_t base,
                                   int32_t id, void* data) {
        auto* self = static_cast<WiFiManager*>(arg);
        
        if (base == WIFI_EVENT) {
            if (id == WIFI_EVENT_STA_START) {
                esp_wifi_connect();
            } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
                if (self->retry_count_ < 5) {
                    esp_wifi_connect();
                    self->retry_count_++;
                } else {
                    xEventGroupSetBits(self->event_group_, FAIL_BIT);
                }
            }
        } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
            self->retry_count_ = 0;
            xEventGroupSetBits(self->event_group_, CONNECTED_BIT);
        }
    }
    
    esp_netif_t* netif_ = nullptr;
    EventGroupHandle_t event_group_ = nullptr;
    esp_event_handler_instance_t wifi_handler_ = nullptr;
    esp_event_handler_instance_t ip_handler_ = nullptr;
    int retry_count_ = 0;
    char ip_address_[16] = {0};
    char mac_address_[18] = {0};
    char hostname_[32] = {0};
};

} // namespace core

#endif // ESP_PLATFORM
