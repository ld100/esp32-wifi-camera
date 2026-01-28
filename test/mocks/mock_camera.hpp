/**
 * @file mock_camera.hpp
 * @brief Mock camera implementation for unit testing
 */
#pragma once

#include "../../main/interfaces/i_camera.hpp"
#include <vector>
#include <cstring>
#include <functional>

namespace mocks {

/**
 * @brief Configurable mock camera for testing
 * 
 * Features:
 * - Configurable capture success/failure
 * - Configurable frame data
 * - Capture delay simulation
 * - Call tracking
 */
class MockCamera : public interfaces::ICamera {
public:
    MockCamera() {
        // Default test frame: simple JPEG-like data
        default_frame_.resize(1024);
        // JPEG magic bytes
        default_frame_[0] = 0xFF;
        default_frame_[1] = 0xD8;
        default_frame_[default_frame_.size() - 2] = 0xFF;
        default_frame_[default_frame_.size() - 1] = 0xD9;
    }
    
    // -------------------------------------------------------------------------
    // ICamera implementation
    // -------------------------------------------------------------------------
    
    bool init(const interfaces::CameraConfig& config) override {
        init_calls_++;
        if (!should_init_succeed_) return false;
        
        config_ = config;
        initialized_ = true;
        return true;
    }
    
    void deinit() override {
        deinit_calls_++;
        initialized_ = false;
        if (current_frame_held_) {
            current_frame_held_ = false;
        }
    }
    
    bool is_initialized() const override { 
        return initialized_; 
    }
    
    interfaces::FrameView capture_frame() override {
        capture_calls_++;
        
        if (!initialized_ || !should_capture_succeed_) {
            return {};
        }
        
        // Simulate capture delay if configured
        if (capture_delay_callback_) {
            capture_delay_callback_();
        }
        
        current_frame_held_ = true;
        frame_counter_++;
        
        interfaces::FrameView view;
        if (custom_frame_data_.empty()) {
            view.data = default_frame_.data();
            view.size = default_frame_.size();
        } else {
            view.data = custom_frame_data_.data();
            view.size = custom_frame_data_.size();
        }
        view.width = get_width_for_resolution(config_.resolution);
        view.height = get_height_for_resolution(config_.resolution);
        view.timestamp_us = frame_counter_ * 33333;  // ~30ms per frame
        
        return view;
    }
    
    void release_frame() override {
        release_calls_++;
        current_frame_held_ = false;
    }
    
    bool set_resolution(interfaces::Resolution res) override {
        if (!initialized_ || !should_set_resolution_succeed_) return false;
        config_.resolution = res;
        return true;
    }
    
    bool set_quality(uint8_t quality) override {
        if (!initialized_ || !should_set_quality_succeed_) return false;
        if (quality < 10 || quality > 63) return false;
        config_.jpeg_quality = quality;
        return true;
    }
    
    interfaces::Resolution get_resolution() const override { 
        return config_.resolution; 
    }
    
    uint8_t get_quality() const override { 
        return config_.jpeg_quality; 
    }
    
    // -------------------------------------------------------------------------
    // Test configuration
    // -------------------------------------------------------------------------
    
    void set_init_result(bool success) { should_init_succeed_ = success; }
    void set_capture_result(bool success) { should_capture_succeed_ = success; }
    void set_resolution_result(bool success) { should_set_resolution_succeed_ = success; }
    void set_quality_result(bool success) { should_set_quality_succeed_ = success; }
    
    void set_custom_frame(const std::vector<uint8_t>& data) {
        custom_frame_data_ = data;
    }
    
    void set_custom_frame(const uint8_t* data, size_t size) {
        custom_frame_data_.assign(data, data + size);
    }
    
    void clear_custom_frame() {
        custom_frame_data_.clear();
    }
    
    // Set callback for simulating capture delay
    void set_capture_delay_callback(std::function<void()> cb) {
        capture_delay_callback_ = cb;
    }
    
    // -------------------------------------------------------------------------
    // Test inspection
    // -------------------------------------------------------------------------
    
    uint32_t init_calls() const { return init_calls_; }
    uint32_t deinit_calls() const { return deinit_calls_; }
    uint32_t capture_calls() const { return capture_calls_; }
    uint32_t release_calls() const { return release_calls_; }
    uint32_t frame_counter() const { return frame_counter_; }
    bool is_frame_held() const { return current_frame_held_; }
    
    void reset_counters() {
        init_calls_ = deinit_calls_ = capture_calls_ = release_calls_ = 0;
        frame_counter_ = 0;
    }

private:
    static uint32_t get_width_for_resolution(interfaces::Resolution res) {
        switch (res) {
            case interfaces::Resolution::QQVGA: return 160;
            case interfaces::Resolution::QVGA:  return 320;
            case interfaces::Resolution::VGA:   return 640;
            case interfaces::Resolution::SVGA:  return 800;
            case interfaces::Resolution::XGA:   return 1024;
            case interfaces::Resolution::HD:    return 1280;
            case interfaces::Resolution::SXGA:  return 1280;
            case interfaces::Resolution::UXGA:  return 1600;
            default: return 640;
        }
    }
    
    static uint32_t get_height_for_resolution(interfaces::Resolution res) {
        switch (res) {
            case interfaces::Resolution::QQVGA: return 120;
            case interfaces::Resolution::QVGA:  return 240;
            case interfaces::Resolution::VGA:   return 480;
            case interfaces::Resolution::SVGA:  return 600;
            case interfaces::Resolution::XGA:   return 768;
            case interfaces::Resolution::HD:    return 720;
            case interfaces::Resolution::SXGA:  return 1024;
            case interfaces::Resolution::UXGA:  return 1200;
            default: return 480;
        }
    }
    
    interfaces::CameraConfig config_;
    bool initialized_ = false;
    bool current_frame_held_ = false;
    
    // Test configuration
    bool should_init_succeed_ = true;
    bool should_capture_succeed_ = true;
    bool should_set_resolution_succeed_ = true;
    bool should_set_quality_succeed_ = true;
    
    // Frame data
    std::vector<uint8_t> default_frame_;
    std::vector<uint8_t> custom_frame_data_;
    
    // Callback for simulating delays
    std::function<void()> capture_delay_callback_;
    
    // Call counters
    uint32_t init_calls_ = 0;
    uint32_t deinit_calls_ = 0;
    uint32_t capture_calls_ = 0;
    uint32_t release_calls_ = 0;
    uint32_t frame_counter_ = 0;
};

} // namespace mocks
