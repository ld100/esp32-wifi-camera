/**
 * @file streaming_service.hpp
 * @brief Producer-consumer streaming service for stable frame rate
 * 
 * Architecture:
 *   [Camera] → [Producer Task] → [FrameBuffer] → [Consumer (HTTP)] → [Browser]
 *              (fixed interval)   (ring buffer)   (blocks for data)
 * 
 * The producer captures frames at a fixed rate (e.g., 3 FPS).
 * The buffer absorbs timing variations from camera and network.
 * The consumer blocks until a frame is available.
 * If buffer overflows, oldest frames are dropped (freshness > history).
 */
#pragma once
#include "../interfaces/i_camera.hpp"
#include "../interfaces/i_clock.hpp"
#include "frame_buffer.hpp"
#include <atomic>
#include <cstdint>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#else
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#endif

namespace core {

struct StreamingConfig {
    uint8_t target_fps = 3;              // Target frame rate
    size_t buffer_slots = 3;              // Ring buffer depth
    size_t max_frame_size = 100 * 1024;   // 100KB max per frame
    uint32_t consumer_timeout_ms = 1000;  // Max wait for frame
};

struct StreamingStats {
    std::atomic<uint32_t> frames_captured{0};
    std::atomic<uint32_t> frames_sent{0};
    std::atomic<uint32_t> frames_dropped{0};
    std::atomic<uint32_t> capture_errors{0};
    std::atomic<bool> producer_running{false};
    
    void reset() {
        frames_captured = 0;
        frames_sent = 0;
        frames_dropped = 0;
        capture_errors = 0;
    }
};

/**
 * @brief Streaming service with producer-consumer architecture
 * 
 * Usage:
 *   StreamingService svc(camera, clock);
 *   svc.init({.target_fps = 3});
 *   svc.start();
 *   
 *   // Consumer loop (e.g., HTTP handler):
 *   while (streaming) {
 *       const uint8_t* data; size_t size;
 *       if (svc.get_frame(&data, &size)) {
 *           send_to_client(data, size);
 *           svc.release_frame();
 *       }
 *   }
 *   
 *   svc.stop();
 */
class StreamingService {
public:
    StreamingService(interfaces::ICamera& camera, interfaces::IClock& clock)
        : camera_(camera), clock_(clock) {}
    
    ~StreamingService() { 
        stop(); 
        deinit();
    }
    
    // Non-copyable
    StreamingService(const StreamingService&) = delete;
    StreamingService& operator=(const StreamingService&) = delete;
    
    /**
     * @brief Initialize the streaming service
     * @param config Configuration parameters
     * @return true on success
     */
    bool init(const StreamingConfig& config = {}) {
        if (initialized_) return true;
        
        config_ = config;
        frame_interval_us_ = 1000000 / config_.target_fps;
        
        if (!buffer_.init(config_.buffer_slots, config_.max_frame_size, true)) {
            return false;
        }
        
#ifdef ESP_PLATFORM
        frame_ready_ = xSemaphoreCreateBinary();
        if (!frame_ready_) {
            buffer_.deinit();
            return false;
        }
#endif
        
        initialized_ = true;
        return true;
    }
    
    void deinit() {
        if (!initialized_) return;
        
        stop();
        buffer_.deinit();
        
#ifdef ESP_PLATFORM
        if (frame_ready_) {
            vSemaphoreDelete(frame_ready_);
            frame_ready_ = nullptr;
        }
#endif
        
        initialized_ = false;
    }
    
    /**
     * @brief Start the producer task
     * @return true on success
     */
    bool start() {
        if (!initialized_) return false;
        if (stats_.producer_running.load()) return true;  // Already running
        
        stop_requested_ = false;
        stats_.reset();
        buffer_.clear();
        buffer_.reset_stats();
        
#ifdef ESP_PLATFORM
        BaseType_t ret = xTaskCreatePinnedToCore(
            producer_task_wrapper,
            "stream_prod",
            4096,
            this,
            5,               // Priority
            &producer_task_,
            1                // Core 1 (leave core 0 for WiFi)
        );
        if (ret != pdPASS) {
            return false;
        }
#else
        producer_thread_ = std::thread(&StreamingService::producer_loop, this);
#endif
        
        return true;
    }
    
    /**
     * @brief Stop the producer task
     */
    void stop() {
        stop_requested_ = true;
        
#ifdef ESP_PLATFORM
        // Wake any waiting consumers
        if (frame_ready_) {
            xSemaphoreGive(frame_ready_);
        }
        
        // Wait for task to finish (with timeout)
        for (int i = 0; i < 50 && stats_.producer_running.load(); i++) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        
        // Force delete if still running
        if (producer_task_ && stats_.producer_running.load()) {
            vTaskDelete(producer_task_);
            stats_.producer_running = false;
        }
        producer_task_ = nullptr;
#else
        // Wake any waiting consumers
        frame_cv_.notify_all();
        
        // Always try to join if thread exists (handles race at startup)
        if (producer_thread_.joinable()) {
            producer_thread_.join();
        }
        stats_.producer_running = false;
#endif
    }
    
    // -------------------------------------------------------------------------
    // Consumer API
    // -------------------------------------------------------------------------
    
    /**
     * @brief Get next frame (blocks until available or timeout)
     * @param data Output: pointer to frame data
     * @param size Output: frame size
     * @param timeout_ms Max time to wait (0 = non-blocking)
     * @return true if frame available
     */
    bool get_frame(const uint8_t** data, size_t* size, uint32_t timeout_ms = 1000) {
        if (!initialized_ || !data || !size) return false;
        
#ifdef ESP_PLATFORM
        TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
        if (xSemaphoreTake(frame_ready_, ticks) != pdTRUE) {
            return false;
        }
#else
        std::unique_lock<std::mutex> lock(frame_mutex_);
        if (timeout_ms == 0) {
            if (buffer_.empty()) return false;
        } else {
            bool result = frame_cv_.wait_for(
                lock, 
                std::chrono::milliseconds(timeout_ms),
                [this] { return !buffer_.empty() || stop_requested_; }
            );
            if (!result || stop_requested_) return false;
        }
#endif
        
        return buffer_.peek(data, size);
    }
    
    /**
     * @brief Release current frame after processing
     */
    void release_frame() {
        buffer_.pop();
        stats_.frames_sent++;
    }
    
    // -------------------------------------------------------------------------
    // Status and Configuration
    // -------------------------------------------------------------------------
    
    const StreamingStats& stats() const { return stats_; }
    size_t buffered_frames() const { return buffer_.available(); }
    bool is_running() const { return stats_.producer_running.load(); }
    bool is_initialized() const { return initialized_; }
    
    void set_target_fps(uint8_t fps) {
        if (fps > 0 && fps <= 30) {
            config_.target_fps = fps;
            frame_interval_us_ = 1000000 / fps;
        }
    }
    
    uint8_t get_target_fps() const { return config_.target_fps; }

private:
#ifdef ESP_PLATFORM
    static void producer_task_wrapper(void* arg) {
        static_cast<StreamingService*>(arg)->producer_loop();
        vTaskDelete(nullptr);
    }
#endif
    
    void producer_loop() {
        stats_.producer_running = true;
        int64_t next_capture_time = clock_.now_us();
        
#ifdef ESP_PLATFORM
        ESP_LOGI("StreamSvc", "Producer started @ %d FPS", config_.target_fps);
#endif
        
        while (!stop_requested_) {
            int64_t now = clock_.now_us();
            
            // Wait until scheduled capture time
            if (now < next_capture_time) {
                int64_t sleep_ms = (next_capture_time - now) / 1000;
                if (sleep_ms > 0) {
                    clock_.delay_ms(static_cast<uint32_t>(sleep_ms));
                }
                continue;
            }
            
            // Capture frame from camera
            auto frame = camera_.capture_frame();
            
            if (frame.valid()) {
                // Push to buffer (may drop oldest if full)
                bool pushed = buffer_.push(frame.data, frame.size, frame.timestamp_us);
                camera_.release_frame();
                
                if (pushed) {
                    stats_.frames_captured++;
                    
                    // Sync dropped frame counter with buffer
                    uint32_t buf_drops = buffer_.frames_dropped();
                    if (buf_drops > stats_.frames_dropped.load()) {
                        stats_.frames_dropped = buf_drops;
                    }
                    
                    // Signal waiting consumers
#ifdef ESP_PLATFORM
                    xSemaphoreGive(frame_ready_);
#else
                    frame_cv_.notify_one();
#endif
                }
            } else {
                stats_.capture_errors++;
                camera_.release_frame();  // Ensure cleanup even on failure
                
#ifdef ESP_PLATFORM
                ESP_LOGW("StreamSvc", "Capture failed, errors=%lu", 
                         stats_.capture_errors.load());
#endif
            }
            
            // Schedule next capture
            next_capture_time += frame_interval_us_;
            
            // If behind schedule, reset to now (don't accumulate delay)
            now = clock_.now_us();
            if (next_capture_time < now) {
                next_capture_time = now + frame_interval_us_;
            }
        }
        
        stats_.producer_running = false;
        
#ifdef ESP_PLATFORM
        ESP_LOGI("StreamSvc", "Producer stopped. Captured=%lu, Sent=%lu, Dropped=%lu",
                 stats_.frames_captured.load(),
                 stats_.frames_sent.load(),
                 stats_.frames_dropped.load());
#endif
    }
    
    // Dependencies (injected)
    interfaces::ICamera& camera_;
    interfaces::IClock& clock_;
    
    // Internal state
    FrameBuffer buffer_;
    StreamingConfig config_;
    StreamingStats stats_;
    
    int64_t frame_interval_us_ = 333333;  // Default 3 FPS
    std::atomic<bool> stop_requested_{false};
    bool initialized_ = false;
    
#ifdef ESP_PLATFORM
    TaskHandle_t producer_task_ = nullptr;
    SemaphoreHandle_t frame_ready_ = nullptr;
#else
    std::thread producer_thread_;
    std::mutex frame_mutex_;
    std::condition_variable frame_cv_;
#endif
};

} // namespace core
