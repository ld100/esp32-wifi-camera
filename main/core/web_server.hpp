/**
 * @file web_server.hpp
 * @brief HTTP server with MJPEG streaming using StreamingService
 * 
 * Simplified web server that:
 * - Serves HTML page with stream view and controls
 * - Provides /stream endpoint consuming from StreamingService
 * - Provides /capture endpoint for single shots
 * - Provides /status endpoint with statistics
 * - Removed FPS counter (unreliable, statistics suffice)
 */
#pragma once

#ifdef ESP_PLATFORM

#include "streaming_service.hpp"
#include "../interfaces/i_camera.hpp"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include <cstring>
#include <cstdio>
#include <atomic>

namespace core {

// MJPEG stream boundary
#define MJPEG_BOUNDARY "frame"
#define MJPEG_CONTENT_TYPE "multipart/x-mixed-replace; boundary=" MJPEG_BOUNDARY

struct WebServerConfig {
    uint16_t port = 80;
    bool single_client_stream = true;
};

struct WebServerStats {
    std::atomic<uint32_t> total_requests{0};
    std::atomic<uint32_t> stream_clients{0};
    std::atomic<uint32_t> captures_served{0};
    int64_t start_time_us = 0;
};

class WebServer {
public:
    WebServer(interfaces::ICamera& camera, StreamingService& streaming)
        : camera_(camera), streaming_(streaming) {
        stats_.start_time_us = esp_timer_get_time();
    }
    
    ~WebServer() { stop(); }
    
    void set_device_info(const char* ip, const char* hostname, const char* mac) {
        if (ip) strncpy(ip_address_, ip, sizeof(ip_address_) - 1);
        if (hostname) strncpy(hostname_, hostname, sizeof(hostname_) - 1);
        if (mac) strncpy(mac_address_, mac, sizeof(mac_address_) - 1);
    }
    
    bool start(const WebServerConfig& config = {}) {
        if (server_) return true;
        
        config_ = config;
        
        httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
        http_config.server_port = config_.port;
        http_config.stack_size = 8192;
        http_config.max_uri_handlers = 8;
        http_config.recv_wait_timeout = 30;
        http_config.send_wait_timeout = 30;
        
        if (httpd_start(&server_, &http_config) != ESP_OK) {
            ESP_LOGE("WebServer", "Failed to start");
            return false;
        }
        
        register_handlers();
        ESP_LOGI("WebServer", "Started on port %d", config_.port);
        return true;
    }
    
    void stop() {
        if (server_) {
            httpd_stop(server_);
            server_ = nullptr;
        }
    }
    
    const WebServerStats& stats() const { return stats_; }

private:
    static constexpr const char* TAG = "WebServer";
    
    // =========================================================================
    // Embedded HTML
    // =========================================================================
    static constexpr const char INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Camera</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; 
               background: #1a1a2e; color: #eee; min-height: 100vh; padding: 20px; }
        .container { max-width: 900px; margin: 0 auto; }
        h1 { text-align: center; margin-bottom: 20px; color: #00d9ff; font-size: 1.5rem; }
        .stream-box { background: #16213e; border-radius: 12px; overflow: hidden; 
                      margin-bottom: 20px; position: relative; }
        .stream-box img { width: 100%; display: block; min-height: 200px; 
                          background: #0f0f23; object-fit: contain; }
        .live-badge { position: absolute; top: 10px; left: 10px; background: #ff4444;
                      color: white; padding: 4px 12px; border-radius: 4px; font-size: 0.8rem;
                      display: none; animation: pulse 2s infinite; }
        @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.5; } }
        .controls { display: flex; gap: 10px; flex-wrap: wrap; margin-bottom: 20px; }
        button { background: #00d9ff; color: #1a1a2e; border: none; padding: 12px 24px;
                 border-radius: 8px; cursor: pointer; font-weight: 600; flex: 1; min-width: 120px;
                 transition: all 0.2s; }
        button:hover { background: #00b8d9; transform: translateY(-2px); }
        button.stop { background: #ff4444; color: white; }
        .stats { background: #16213e; border-radius: 12px; padding: 15px; }
        .stats h3 { margin-bottom: 10px; color: #00d9ff; font-size: 1rem; }
        .stat-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(120px, 1fr)); gap: 10px; }
        .stat { background: #0f0f23; padding: 10px; border-radius: 8px; text-align: center; }
        .stat-value { font-size: 1.2rem; font-weight: bold; color: #00d9ff; }
        .stat-label { font-size: 0.75rem; color: #888; margin-top: 2px; }
        .config { background: #16213e; border-radius: 12px; padding: 15px; margin-bottom: 20px; }
        .config label { display: block; margin-bottom: 5px; font-size: 0.9rem; color: #888; }
        .config select { width: 100%; padding: 8px; border-radius: 6px; border: none;
                        background: #0f0f23; color: #eee; margin-bottom: 10px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>ESP32-S3 Camera</h1>
        <div class="stream-box">
            <span id="live-badge" class="live-badge">LIVE</span>
            <img id="stream" alt="Stream">
        </div>
        <div class="controls">
            <button id="btn-stream" onclick="toggleStream()">Start Stream</button>
            <button onclick="capturePhoto()">Capture</button>
            <button onclick="downloadCapture()">Download</button>
        </div>
        <div class="config">
            <label>Resolution</label>
            <select id="resolution" onchange="updateConfig()">
                <option value="0">QQVGA (160x120)</option>
                <option value="1">QVGA (320x240)</option>
                <option value="2" selected>VGA (640x480)</option>
                <option value="3">SVGA (800x600)</option>
                <option value="4">XGA (1024x768)</option>
            </select>
            <label>Quality (lower = better)</label>
            <select id="quality" onchange="updateConfig()">
                <option value="10">10 (Best)</option>
                <option value="15">15</option>
                <option value="20" selected>20</option>
                <option value="25">25</option>
                <option value="30">30 (Fast)</option>
            </select>
        </div>
        <div class="stats">
            <h3>Statistics</h3>
            <div class="stat-grid">
                <div class="stat"><div class="stat-value" id="captured">0</div><div class="stat-label">Captured</div></div>
                <div class="stat"><div class="stat-value" id="sent">0</div><div class="stat-label">Sent</div></div>
                <div class="stat"><div class="stat-value" id="dropped">0</div><div class="stat-label">Dropped</div></div>
                <div class="stat"><div class="stat-value" id="buffered">0</div><div class="stat-label">Buffered</div></div>
                <div class="stat"><div class="stat-value" id="heap">0</div><div class="stat-label">Heap (KB)</div></div>
                <div class="stat"><div class="stat-value" id="rssi">--</div><div class="stat-label">RSSI</div></div>
            </div>
        </div>
    </div>
    <script>
        let streaming = false;
        let statsInterval = null;
        
        function toggleStream() {
            const btn = document.getElementById('btn-stream');
            const img = document.getElementById('stream');
            const badge = document.getElementById('live-badge');
            
            if (streaming) {
                img.src = '';
                btn.textContent = 'Start Stream';
                btn.classList.remove('stop');
                badge.style.display = 'none';
                streaming = false;
            } else {
                img.src = '/stream?' + Date.now();
                btn.textContent = 'Stop Stream';
                btn.classList.add('stop');
                badge.style.display = 'block';
                streaming = true;
            }
        }
        
        function capturePhoto() {
            document.getElementById('stream').src = '/capture?' + Date.now();
        }
        
        function downloadCapture() {
            const link = document.createElement('a');
            link.href = '/capture?' + Date.now();
            link.download = 'capture_' + Date.now() + '.jpg';
            link.click();
        }
        
        async function updateConfig() {
            const res = document.getElementById('resolution').value;
            const qual = document.getElementById('quality').value;
            try {
                await fetch('/config', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                    body: `resolution=${res}&quality=${qual}`
                });
            } catch (e) { console.error('Config error:', e); }
        }
        
        async function updateStats() {
            try {
                const response = await fetch('/status');
                const data = await response.json();
                document.getElementById('captured').textContent = data.captured || 0;
                document.getElementById('sent').textContent = data.sent || 0;
                document.getElementById('dropped').textContent = data.dropped || 0;
                document.getElementById('buffered').textContent = data.buffered || 0;
                document.getElementById('heap').textContent = Math.floor((data.heap || 0) / 1024);
                document.getElementById('rssi').textContent = data.rssi || '--';
                document.getElementById('resolution').value = data.resolution || 2;
                document.getElementById('quality').value = data.quality || 20;
            } catch (e) { console.error('Stats error:', e); }
        }
        
        updateStats();
        statsInterval = setInterval(updateStats, 2000);
        
        document.getElementById('stream').onerror = function() {
            if (streaming) {
                setTimeout(() => { if (streaming) this.src = '/stream?' + Date.now(); }, 1000);
            }
        };
    </script>
</body>
</html>
)rawliteral";

    // =========================================================================
    // Handlers
    // =========================================================================
    void register_handlers() {
        httpd_uri_t uri_index = { .uri = "/", .method = HTTP_GET, 
                                  .handler = index_handler, .user_ctx = this };
        httpd_register_uri_handler(server_, &uri_index);
        
        httpd_uri_t uri_stream = { .uri = "/stream", .method = HTTP_GET,
                                   .handler = stream_handler, .user_ctx = this };
        httpd_register_uri_handler(server_, &uri_stream);
        
        httpd_uri_t uri_capture = { .uri = "/capture", .method = HTTP_GET,
                                    .handler = capture_handler, .user_ctx = this };
        httpd_register_uri_handler(server_, &uri_capture);
        
        httpd_uri_t uri_status = { .uri = "/status", .method = HTTP_GET,
                                   .handler = status_handler, .user_ctx = this };
        httpd_register_uri_handler(server_, &uri_status);
        
        httpd_uri_t uri_config = { .uri = "/config", .method = HTTP_POST,
                                   .handler = config_handler, .user_ctx = this };
        httpd_register_uri_handler(server_, &uri_config);
    }
    
    static esp_err_t index_handler(httpd_req_t* req) {
        auto* self = static_cast<WebServer*>(req->user_ctx);
        self->stats_.total_requests++;
        httpd_resp_set_type(req, "text/html");
        return httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
    }
    
    static esp_err_t stream_handler(httpd_req_t* req) {
        auto* self = static_cast<WebServer*>(req->user_ctx);
        self->stats_.total_requests++;
        
        // Single client check
        if (self->config_.single_client_stream && self->stats_.stream_clients.load() > 0) {
            httpd_resp_set_status(req, "503 Service Unavailable");
            return httpd_resp_send(req, "Stream busy", HTTPD_RESP_USE_STRLEN);
        }
        
        self->stats_.stream_clients++;
        ESP_LOGI(TAG, "Stream client connected");
        
        httpd_resp_set_type(req, MJPEG_CONTENT_TYPE);
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
        
        char part_header[128];
        
        while (true) {
            const uint8_t* data = nullptr;
            size_t size = 0;
            
            // Get frame from streaming service (blocks until available)
            if (!self->streaming_.get_frame(&data, &size, 500)) {
                // Timeout - check if we should continue
                if (!self->streaming_.is_running()) break;
                continue;
            }
            
            // Send MJPEG part header
            int hdr_len = snprintf(part_header, sizeof(part_header),
                "\r\n--" MJPEG_BOUNDARY "\r\n"
                "Content-Type: image/jpeg\r\n"
                "Content-Length: %zu\r\n\r\n", size);
            
            esp_err_t res = httpd_resp_send_chunk(req, part_header, hdr_len);
            if (res != ESP_OK) {
                self->streaming_.release_frame();
                break;
            }
            
            // Send frame data
            res = httpd_resp_send_chunk(req, reinterpret_cast<const char*>(data), size);
            self->streaming_.release_frame();
            
            if (res != ESP_OK) break;
        }
        
        self->stats_.stream_clients--;
        ESP_LOGI(TAG, "Stream client disconnected");
        return ESP_OK;
    }
    
    static esp_err_t capture_handler(httpd_req_t* req) {
        auto* self = static_cast<WebServer*>(req->user_ctx);
        self->stats_.total_requests++;
        
        auto frame = self->camera_.capture_frame();
        if (!frame.valid()) {
            return httpd_resp_send_500(req);
        }
        
        httpd_resp_set_type(req, "image/jpeg");
        httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
        esp_err_t res = httpd_resp_send(req, reinterpret_cast<const char*>(frame.data), frame.size);
        
        self->camera_.release_frame();
        self->stats_.captures_served++;
        return res;
    }
    
    static esp_err_t status_handler(httpd_req_t* req) {
        auto* self = static_cast<WebServer*>(req->user_ctx);
        self->stats_.total_requests++;
        
        // Get RSSI
        wifi_ap_record_t ap_info;
        int rssi = 0;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            rssi = ap_info.rssi;
        }
        
        auto& stream_stats = self->streaming_.stats();
        
        char json[256];
        int len = snprintf(json, sizeof(json),
            "{\"captured\":%lu,\"sent\":%lu,\"dropped\":%lu,\"buffered\":%zu,"
            "\"heap\":%lu,\"rssi\":%d,\"resolution\":%d,\"quality\":%d,\"streaming\":%s}",
            stream_stats.frames_captured.load(),
            stream_stats.frames_sent.load(),
            stream_stats.frames_dropped.load(),
            self->streaming_.buffered_frames(),
            esp_get_free_heap_size(),
            rssi,
            static_cast<int>(self->camera_.get_resolution()),
            self->camera_.get_quality(),
            self->streaming_.is_running() ? "true" : "false"
        );
        
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, json, len);
    }
    
    static esp_err_t config_handler(httpd_req_t* req) {
        auto* self = static_cast<WebServer*>(req->user_ctx);
        self->stats_.total_requests++;
        
        char buf[64];
        int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (ret <= 0) return httpd_resp_send_500(req);
        buf[ret] = '\0';
        
        int res = 2, qual = 20;
        sscanf(buf, "resolution=%d&quality=%d", &res, &qual);
        
        self->camera_.set_resolution(static_cast<interfaces::Resolution>(res));
        self->camera_.set_quality(static_cast<uint8_t>(qual));
        
        return httpd_resp_send(req, "OK", 2);
    }
    
    // Members
    interfaces::ICamera& camera_;
    StreamingService& streaming_;
    httpd_handle_t server_ = nullptr;
    WebServerConfig config_;
    WebServerStats stats_;
    char ip_address_[16] = {0};
    char hostname_[32] = {0};
    char mac_address_[18] = {0};
};

} // namespace core

#endif // ESP_PLATFORM
