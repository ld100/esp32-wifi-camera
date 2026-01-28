/**
 * @file i_clock.hpp
 * @brief Clock/timing interface for testable time operations
 */
#pragma once
#include <cstdint>

namespace interfaces {

/**
 * @brief Abstract clock interface
 * 
 * Abstracts time and delay operations for deterministic testing.
 * Production: wraps esp_timer + vTaskDelay
 * Testing: mock with controllable time
 */
class IClock {
public:
    virtual ~IClock() = default;
    
    // Current time
    virtual int64_t now_us() const = 0;
    virtual int64_t now_ms() const { return now_us() / 1000; }
    
    // Delays
    virtual void delay_ms(uint32_t ms) = 0;
    virtual void delay_us(uint32_t us) = 0;
    
    // Yield to other tasks (no guaranteed delay)
    virtual void yield() = 0;
};

} // namespace interfaces
