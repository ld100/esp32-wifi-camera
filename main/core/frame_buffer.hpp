/**
 * @file frame_buffer.hpp
 * @brief Thread-safe circular buffer for smooth streaming
 * 
 * Design: Fixed-size ring buffer with overflow policy (drop oldest).
 * Cross-platform: Uses FreeRTOS primitives on ESP32, std::mutex on host.
 */
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <atomic>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#else
#include <mutex>
#include <vector>
#endif

namespace core {

struct FrameSlot {
    uint8_t* data = nullptr;
    size_t capacity = 0;
    size_t size = 0;
    int64_t timestamp_us = 0;
    bool occupied = false;
    bool reading = false;  // Consumer is reading this slot
};

/**
 * @brief Thread-safe circular frame buffer
 * 
 * Push: Adds frame to buffer. If full, drops oldest frame.
 * Peek: Returns pointer to oldest frame without removing.
 * Pop: Removes oldest frame from buffer.
 * 
 * Memory: Pre-allocates slots in PSRAM (ESP32) or heap (host).
 */
class FrameBuffer {
public:
    static constexpr size_t DEFAULT_SLOTS = 3;
    static constexpr size_t DEFAULT_FRAME_SIZE = 100 * 1024;  // 100KB

    FrameBuffer() = default;
    ~FrameBuffer() { deinit(); }
    
    // Non-copyable
    FrameBuffer(const FrameBuffer&) = delete;
    FrameBuffer& operator=(const FrameBuffer&) = delete;
    
    /**
     * @brief Initialize buffer with pre-allocated slots
     * @param num_slots Number of frame slots (ring buffer depth)
     * @param max_frame_size Maximum bytes per frame
     * @param use_psram Use PSRAM for allocation (ESP32 only)
     * @return true on success
     */
    bool init(size_t num_slots = DEFAULT_SLOTS, 
              size_t max_frame_size = DEFAULT_FRAME_SIZE, 
              bool use_psram = true) {
        if (initialized_) return true;
        if (num_slots == 0 || max_frame_size == 0) return false;
        
        num_slots_ = num_slots;
        max_frame_size_ = max_frame_size;
        
        // Allocate slot metadata array
        slots_ = new (std::nothrow) FrameSlot[num_slots_];
        if (!slots_) return false;
        
        // Allocate frame data buffers
        for (size_t i = 0; i < num_slots_; i++) {
#ifdef ESP_PLATFORM
            if (use_psram) {
                slots_[i].data = static_cast<uint8_t*>(
                    heap_caps_malloc(max_frame_size_, MALLOC_CAP_SPIRAM));
            } else {
                slots_[i].data = static_cast<uint8_t*>(malloc(max_frame_size_));
            }
#else
            (void)use_psram;
            slots_[i].data = static_cast<uint8_t*>(malloc(max_frame_size_));
#endif
            if (!slots_[i].data) {
                deinit();
                return false;
            }
            slots_[i].capacity = max_frame_size_;
        }
        
#ifdef ESP_PLATFORM
        mutex_ = xSemaphoreCreateMutex();
        if (!mutex_) {
            deinit();
            return false;
        }
#endif
        
        initialized_ = true;
        return true;
    }
    
    void deinit() {
        if (slots_) {
            for (size_t i = 0; i < num_slots_; i++) {
                if (slots_[i].data) {
#ifdef ESP_PLATFORM
                    heap_caps_free(slots_[i].data);
#else
                    free(slots_[i].data);
#endif
                    slots_[i].data = nullptr;
                }
            }
            delete[] slots_;
            slots_ = nullptr;
        }
        
#ifdef ESP_PLATFORM
        if (mutex_) {
            vSemaphoreDelete(mutex_);
            mutex_ = nullptr;
        }
#endif
        
        num_slots_ = 0;
        max_frame_size_ = 0;
        write_idx_ = 0;
        read_idx_ = 0;
        count_ = 0;
        frames_dropped_ = 0;
        initialized_ = false;
    }
    
    /**
     * @brief Push a frame into the buffer
     * @param data Frame data (copied)
     * @param size Frame size in bytes
     * @param timestamp_us Frame timestamp
     * @return true on success, false if data is null/too large/buffer not initialized/full with locked slot
     */
    bool push(const uint8_t* data, size_t size, int64_t timestamp_us = 0) {
        if (!initialized_ || !data || size == 0) return false;
        if (size > max_frame_size_) return false;
        
        lock();
        
        // If buffer full, try to drop oldest frame
        if (count_ >= num_slots_) {
            // Don't drop the slot being read by consumer
            if (slots_[read_idx_].reading) {
                // Buffer truly full (consumer holding oldest slot)
                // Drop this new frame instead
                unlock();
                frames_dropped_++;
                return true;  // Return true so caller doesn't retry immediately
            }
            slots_[read_idx_].occupied = false;
            read_idx_ = (read_idx_ + 1) % num_slots_;
            count_--;
            frames_dropped_++;
        }
        
        // Write to next slot
        FrameSlot& slot = slots_[write_idx_];
        memcpy(slot.data, data, size);
        slot.size = size;
        slot.timestamp_us = timestamp_us;
        slot.occupied = true;
        slot.reading = false;
        
        write_idx_ = (write_idx_ + 1) % num_slots_;
        count_++;
        
        unlock();
        return true;
    }
    
    /**
     * @brief Peek at oldest frame without removing (marks slot as being read)
     * @param data Output: pointer to frame data
     * @param size Output: frame size
     * @param timestamp_us Output: frame timestamp (optional)
     * @return true if frame available, false if buffer empty
     * @note Caller MUST call pop() after done reading to release the slot
     */
    bool peek(const uint8_t** data, size_t* size, int64_t* timestamp_us = nullptr) {
        if (!initialized_ || !data || !size) return false;
        
        lock();
        
        if (count_ == 0) {
            unlock();
            return false;
        }
        
        FrameSlot& slot = slots_[read_idx_];
        slot.reading = true;  // Mark as being read (prevents overflow from dropping)
        *data = slot.data;
        *size = slot.size;
        if (timestamp_us) {
            *timestamp_us = slot.timestamp_us;
        }
        
        unlock();
        return true;
    }
    
    /**
     * @brief Remove oldest frame from buffer (releases read lock)
     */
    void pop() {
        if (!initialized_) return;
        
        lock();
        
        if (count_ > 0) {
            slots_[read_idx_].occupied = false;
            slots_[read_idx_].reading = false;  // Release read lock
            read_idx_ = (read_idx_ + 1) % num_slots_;
            count_--;
        }
        
        unlock();
    }
    
    // Status queries (lock-free reads)
    size_t available() const { return count_.load(); }
    bool empty() const { return count_.load() == 0; }
    bool full() const { return initialized_ && count_.load() >= num_slots_; }
    uint32_t frames_dropped() const { return frames_dropped_.load(); }
    size_t capacity() const { return num_slots_; }
    size_t max_frame_size() const { return max_frame_size_; }
    bool is_initialized() const { return initialized_; }
    
    /**
     * @brief Clear all frames from buffer
     */
    void clear() {
        if (!initialized_) return;
        
        lock();
        for (size_t i = 0; i < num_slots_; i++) {
            slots_[i].occupied = false;
            slots_[i].reading = false;
        }
        read_idx_ = 0;
        write_idx_ = 0;
        count_ = 0;
        unlock();
    }
    
    /**
     * @brief Reset dropped frame counter
     */
    void reset_stats() {
        frames_dropped_ = 0;
    }

private:
    void lock() {
#ifdef ESP_PLATFORM
        xSemaphoreTake(mutex_, portMAX_DELAY);
#else
        mutex_.lock();
#endif
    }
    
    void unlock() {
#ifdef ESP_PLATFORM
        xSemaphoreGive(mutex_);
#else
        mutex_.unlock();
#endif
    }
    
    FrameSlot* slots_ = nullptr;
    size_t num_slots_ = 0;
    size_t max_frame_size_ = 0;
    size_t write_idx_ = 0;
    size_t read_idx_ = 0;
    std::atomic<size_t> count_{0};
    std::atomic<uint32_t> frames_dropped_{0};
    bool initialized_ = false;
    
#ifdef ESP_PLATFORM
    SemaphoreHandle_t mutex_ = nullptr;
#else
    std::mutex mutex_;
#endif
};

} // namespace core
