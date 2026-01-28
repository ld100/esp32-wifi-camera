/**
 * @file esp_clock_driver.hpp
 * @brief ESP32 clock driver implementing IClock interface
 */
#pragma once

#ifdef ESP_PLATFORM

#include "../interfaces/i_clock.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"

namespace drivers {

class EspClockDriver : public interfaces::IClock {
public:
    int64_t now_us() const override {
        return esp_timer_get_time();
    }
    
    void delay_ms(uint32_t ms) override {
        if (ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(ms));
        }
    }
    
    void delay_us(uint32_t us) override {
        if (us > 0) {
            ets_delay_us(us);
        }
    }
    
    void yield() override {
        taskYIELD();
    }
};

} // namespace drivers

#endif // ESP_PLATFORM
