/**
 * @file esp_camera_driver.hpp
 * @brief ESP32 camera driver implementing ICamera interface
 */
#pragma once

#ifdef ESP_PLATFORM

#include "../interfaces/i_camera.hpp"
#include "esp_camera.h"
#include "esp_log.h"
#include <atomic>

namespace drivers {

// ESP32-S3-EYE pin configuration (adjust for your board)
struct CameraPins {
    int pwdn = -1;
    int reset = -1;
    int xclk = 15;
    int siod = 4;
    int sioc = 5;
    int d7 = 16, d6 = 17, d5 = 18, d4 = 12, d3 = 10, d2 = 8, d1 = 9, d0 = 11;
    int vsync = 6;
    int href = 7;
    int pclk = 13;
};

class EspCameraDriver : public interfaces::ICamera {
public:
    explicit EspCameraDriver(const CameraPins& pins = {}) : pins_(pins) {}
    
    ~EspCameraDriver() override { deinit(); }
    
    bool init(const interfaces::CameraConfig& config) override {
        if (initialized_) return true;
        
        config_ = config;
        
        camera_config_t cam_cfg = {};
        cam_cfg.pin_pwdn = pins_.pwdn;
        cam_cfg.pin_reset = pins_.reset;
        cam_cfg.pin_xclk = pins_.xclk;
        cam_cfg.pin_sccb_sda = pins_.siod;
        cam_cfg.pin_sccb_scl = pins_.sioc;
        cam_cfg.pin_d7 = pins_.d7;
        cam_cfg.pin_d6 = pins_.d6;
        cam_cfg.pin_d5 = pins_.d5;
        cam_cfg.pin_d4 = pins_.d4;
        cam_cfg.pin_d3 = pins_.d3;
        cam_cfg.pin_d2 = pins_.d2;
        cam_cfg.pin_d1 = pins_.d1;
        cam_cfg.pin_d0 = pins_.d0;
        cam_cfg.pin_vsync = pins_.vsync;
        cam_cfg.pin_href = pins_.href;
        cam_cfg.pin_pclk = pins_.pclk;
        
        cam_cfg.xclk_freq_hz = 20000000;
        cam_cfg.ledc_timer = LEDC_TIMER_0;
        cam_cfg.ledc_channel = LEDC_CHANNEL_0;
        cam_cfg.pixel_format = PIXFORMAT_JPEG;
        cam_cfg.frame_size = resolution_to_framesize(config_.resolution);
        cam_cfg.jpeg_quality = config_.jpeg_quality;
        cam_cfg.fb_count = config_.frame_buffer_count;
        cam_cfg.fb_location = CAMERA_FB_IN_PSRAM;
        cam_cfg.grab_mode = CAMERA_GRAB_LATEST;
        
        esp_err_t err = esp_camera_init(&cam_cfg);
        if (err != ESP_OK) {
            ESP_LOGE("CamDriver", "Init failed: %s", esp_err_to_name(err));
            return false;
        }
        
        initialized_ = true;
        ESP_LOGI("CamDriver", "Initialized: %dx%d, Q=%d",
                 get_width(), get_height(), config_.jpeg_quality);
        return true;
    }
    
    void deinit() override {
        if (initialized_) {
            if (current_fb_) {
                esp_camera_fb_return(current_fb_);
                current_fb_ = nullptr;
            }
            esp_camera_deinit();
            initialized_ = false;
        }
    }
    
    bool is_initialized() const override { return initialized_; }
    
    interfaces::FrameView capture_frame() override {
        if (!initialized_) return {};
        
        // Release previous frame if not already released
        if (current_fb_) {
            esp_camera_fb_return(current_fb_);
            current_fb_ = nullptr;
        }
        
        current_fb_ = esp_camera_fb_get();
        if (!current_fb_) {
            ESP_LOGW("CamDriver", "fb_get failed");
            return {};
        }
        
        interfaces::FrameView view;
        view.data = current_fb_->buf;
        view.size = current_fb_->len;
        view.width = current_fb_->width;
        view.height = current_fb_->height;
        view.timestamp_us = current_fb_->timestamp.tv_sec * 1000000LL + 
                           current_fb_->timestamp.tv_usec;
        return view;
    }
    
    void release_frame() override {
        if (current_fb_) {
            esp_camera_fb_return(current_fb_);
            current_fb_ = nullptr;
        }
    }
    
    bool set_resolution(interfaces::Resolution res) override {
        if (!initialized_) return false;
        
        sensor_t* sensor = esp_camera_sensor_get();
        if (!sensor) return false;
        
        if (sensor->set_framesize(sensor, resolution_to_framesize(res)) != 0) {
            return false;
        }
        
        config_.resolution = res;
        return true;
    }
    
    bool set_quality(uint8_t quality) override {
        if (!initialized_ || quality < 10 || quality > 63) return false;
        
        sensor_t* sensor = esp_camera_sensor_get();
        if (!sensor) return false;
        
        if (sensor->set_quality(sensor, quality) != 0) {
            return false;
        }
        
        config_.jpeg_quality = quality;
        return true;
    }
    
    interfaces::Resolution get_resolution() const override { 
        return config_.resolution; 
    }
    
    uint8_t get_quality() const override { 
        return config_.jpeg_quality; 
    }

private:
    static framesize_t resolution_to_framesize(interfaces::Resolution res) {
        switch (res) {
            case interfaces::Resolution::QQVGA: return FRAMESIZE_QQVGA;
            case interfaces::Resolution::QVGA:  return FRAMESIZE_QVGA;
            case interfaces::Resolution::VGA:   return FRAMESIZE_VGA;
            case interfaces::Resolution::SVGA:  return FRAMESIZE_SVGA;
            case interfaces::Resolution::XGA:   return FRAMESIZE_XGA;
            case interfaces::Resolution::HD:    return FRAMESIZE_HD;
            case interfaces::Resolution::SXGA:  return FRAMESIZE_SXGA;
            case interfaces::Resolution::UXGA:  return FRAMESIZE_UXGA;
            default: return FRAMESIZE_VGA;
        }
    }
    
    uint32_t get_width() const {
        switch (config_.resolution) {
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
    
    uint32_t get_height() const {
        switch (config_.resolution) {
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
    
    CameraPins pins_;
    interfaces::CameraConfig config_;
    camera_fb_t* current_fb_ = nullptr;
    bool initialized_ = false;
};

} // namespace drivers

#endif // ESP_PLATFORM
