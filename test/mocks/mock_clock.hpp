/**
 * @file mock_clock.hpp
 * @brief Mock clock implementation for deterministic testing
 */
#pragma once

#include "../../main/interfaces/i_clock.hpp"
#include <functional>
#include <thread>
#include <chrono>

namespace mocks {

/**
 * @brief Controllable mock clock for testing
 * 
 * Features:
 * - Manual time advancement
 * - Auto-increment on now_us() calls
 * - Delay callback for test synchronization
 * - Call tracking
 * - Optional real micro-sleep for thread coordination
 */
class MockClock : public interfaces::IClock {
public:
    // -------------------------------------------------------------------------
    // IClock implementation
    // -------------------------------------------------------------------------
    
    int64_t now_us() const override {
        now_calls_++;
        int64_t result = current_time_us_;
        if (auto_advance_us_ > 0) {
            current_time_us_ += auto_advance_us_;
        }
        return result;
    }
    
    void delay_ms(uint32_t ms) override {
        delay_ms_calls_++;
        total_delay_ms_ += ms;
        
        // Advance mock time by delay amount
        current_time_us_ += static_cast<int64_t>(ms) * 1000;
        
        // Small real sleep to yield CPU and allow test thread to observe state
        // Without this, producer thread spins too fast for reliable testing
        if (real_sleep_enabled_) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        
        // Call delay callback if set (for test synchronization)
        if (delay_callback_) {
            delay_callback_(ms);
        }
    }
    
    void delay_us(uint32_t us) override {
        delay_us_calls_++;
        total_delay_us_ += us;
        current_time_us_ += us;
        
        if (real_sleep_enabled_ && us >= 1000) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }
    
    void yield() override {
        yield_calls_++;
        
        if (real_sleep_enabled_) {
            std::this_thread::yield();
        }
        
        if (yield_callback_) {
            yield_callback_();
        }
    }
    
    // -------------------------------------------------------------------------
    // Test control
    // -------------------------------------------------------------------------
    
    // Set absolute time
    void set_time_us(int64_t time_us) {
        current_time_us_ = time_us;
    }
    
    void set_time_ms(int64_t time_ms) {
        current_time_us_ = time_ms * 1000;
    }
    
    // Advance time manually
    void advance_us(int64_t us) {
        current_time_us_ += us;
    }
    
    void advance_ms(int64_t ms) {
        current_time_us_ += ms * 1000;
    }
    
    // Auto-advance time on each now_us() call
    void set_auto_advance_us(int64_t us) {
        auto_advance_us_ = us;
    }
    
    void set_auto_advance_ms(int64_t ms) {
        auto_advance_us_ = ms * 1000;
    }
    
    // Callbacks for test synchronization
    void set_delay_callback(std::function<void(uint32_t)> cb) {
        delay_callback_ = cb;
    }
    
    void set_yield_callback(std::function<void()> cb) {
        yield_callback_ = cb;
    }
    
    // Enable/disable real micro-sleeps for thread coordination (default: enabled)
    void set_real_sleep(bool enabled) {
        real_sleep_enabled_ = enabled;
    }
    
    // -------------------------------------------------------------------------
    // Test inspection
    // -------------------------------------------------------------------------
    
    int64_t current_time() const { return current_time_us_; }
    
    uint32_t now_calls() const { return now_calls_; }
    uint32_t delay_ms_calls() const { return delay_ms_calls_; }
    uint32_t delay_us_calls() const { return delay_us_calls_; }
    uint32_t yield_calls() const { return yield_calls_; }
    
    uint64_t total_delay_ms() const { return total_delay_ms_; }
    uint64_t total_delay_us() const { return total_delay_us_; }
    
    void reset() {
        current_time_us_ = 0;
        auto_advance_us_ = 0;
        now_calls_ = 0;
        delay_ms_calls_ = 0;
        delay_us_calls_ = 0;
        yield_calls_ = 0;
        total_delay_ms_ = 0;
        total_delay_us_ = 0;
        delay_callback_ = nullptr;
        yield_callback_ = nullptr;
    }

private:
    mutable int64_t current_time_us_ = 0;
    mutable int64_t auto_advance_us_ = 0;
    
    // Call counters
    mutable uint32_t now_calls_ = 0;
    uint32_t delay_ms_calls_ = 0;
    uint32_t delay_us_calls_ = 0;
    uint32_t yield_calls_ = 0;
    
    // Accumulated delays
    uint64_t total_delay_ms_ = 0;
    uint64_t total_delay_us_ = 0;
    
    // Callbacks
    std::function<void(uint32_t)> delay_callback_;
    std::function<void()> yield_callback_;
    
    // Real sleep for thread coordination (default enabled)
    bool real_sleep_enabled_ = true;
};

} // namespace mocks
