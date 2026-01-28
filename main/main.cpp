/**
 * @file main.cpp
 * @brief ESP32 WiFi Camera - Main entry point
 * 
 * Architecture:
 *   [Camera Driver] → [Streaming Service] → [Web Server] → [Browser]
 *   (ICamera impl)    (Producer-Consumer)   (HTTP+MJPEG)
 * 
 * All components use dependency injection for testability.
 */

#include "drivers/esp_camera_driver.hpp"
#include "drivers/esp_clock_driver.hpp"
#include "core/wifi_manager.hpp"
#include "core/streaming_service.hpp"
#include "core/web_server.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "main";

// Configuration from Kconfig (with fallback defaults)
#include "sdkconfig.h"

#ifndef CONFIG_WIFI_SSID
#define CONFIG_WIFI_SSID "your_ssid"
#endif

#ifndef CONFIG_WIFI_PASSWORD
#define CONFIG_WIFI_PASSWORD "your_password"
#endif

#ifndef CONFIG_MDNS_HOSTNAME
#define CONFIG_MDNS_HOSTNAME "espcam"
#endif

#ifndef CONFIG_STREAM_FPS
#define CONFIG_STREAM_FPS 8
#endif

#ifndef CONFIG_CAMERA_JPEG_QUALITY
#define CONFIG_CAMERA_JPEG_QUALITY 12
#endif

#ifndef CONFIG_CAMERA_FRAME_BUFFERS
#define CONFIG_CAMERA_FRAME_BUFFERS 2
#endif

#ifndef CONFIG_STREAM_BUFFER_SLOTS
#define CONFIG_STREAM_BUFFER_SLOTS 4
#endif

#ifndef CONFIG_STREAM_MAX_FRAME_SIZE
#define CONFIG_STREAM_MAX_FRAME_SIZE 102400
#endif

extern "C" void app_main() {
    ESP_LOGI(TAG, "=== ESP32-S3 WiFi Camera ===");
    ESP_LOGI(TAG, "Architecture: Dependency Injection + Producer-Consumer");
    
    // =========================================================================
    // 1. Create drivers (hardware abstraction)
    // =========================================================================
    drivers::CameraPins pins;  // Uses default ESP32-S3-EYE pins
    drivers::EspCameraDriver camera(pins);
    drivers::EspClockDriver clock;
    
    // =========================================================================
    // 2. Initialize camera
    // =========================================================================
    interfaces::CameraConfig cam_config;
    cam_config.resolution = interfaces::Resolution::VGA;
    cam_config.jpeg_quality = CONFIG_CAMERA_JPEG_QUALITY;
    cam_config.frame_buffer_count = CONFIG_CAMERA_FRAME_BUFFERS;
    
    if (!camera.init(cam_config)) {
        ESP_LOGE(TAG, "Camera init failed!");
        return;
    }
    ESP_LOGI(TAG, "Camera initialized");
    
    // =========================================================================
    // 3. Connect to WiFi
    // =========================================================================
    core::WiFiManager wifi;
    if (!wifi.init()) {
        ESP_LOGE(TAG, "WiFi init failed!");
        return;
    }
    
    if (!wifi.connect(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD)) {
        ESP_LOGE(TAG, "WiFi connection failed!");
        return;
    }
    
    wifi.start_mdns(CONFIG_MDNS_HOSTNAME);
    
    // =========================================================================
    // 4. Initialize streaming service (producer-consumer pipeline)
    // =========================================================================
    core::StreamingService streaming(camera, clock);
    
    core::StreamingConfig stream_config;
    stream_config.target_fps = CONFIG_STREAM_FPS;
    stream_config.buffer_slots = CONFIG_STREAM_BUFFER_SLOTS;
    stream_config.max_frame_size = CONFIG_STREAM_MAX_FRAME_SIZE;
    
    if (!streaming.init(stream_config)) {
        ESP_LOGE(TAG, "Streaming service init failed!");
        return;
    }
    
    // Start the producer task
    if (!streaming.start()) {
        ESP_LOGE(TAG, "Streaming service start failed!");
        return;
    }
    ESP_LOGI(TAG, "Streaming service started @ %d FPS (buffer=%zu)", 
             stream_config.target_fps, stream_config.buffer_slots);
    
    // =========================================================================
    // 5. Start web server
    // =========================================================================
    core::WebServer server(camera, streaming);
    server.set_device_info(wifi.ip_address(), wifi.hostname(), wifi.mac_address());
    
    if (!server.start()) {
        ESP_LOGE(TAG, "Web server start failed!");
        return;
    }
    
    // =========================================================================
    // Ready!
    // =========================================================================
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Ready! Access at:");
    ESP_LOGI(TAG, "  http://%s/", wifi.ip_address());
    ESP_LOGI(TAG, "  http://%s.local/", wifi.hostname());
    ESP_LOGI(TAG, "========================================");
    
    // Keep main task alive and log stats periodically
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(30000));  // Every 30 seconds
        
        auto& stats = streaming.stats();
        ESP_LOGI(TAG, "Stats: captured=%lu sent=%lu dropped=%lu errors=%lu heap=%lu",
                 stats.frames_captured.load(),
                 stats.frames_sent.load(),
                 stats.frames_dropped.load(),
                 stats.capture_errors.load(),
                 esp_get_free_heap_size());
    }
}
