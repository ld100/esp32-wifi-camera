/**
 * @file i_camera.hpp
 * @brief Camera interface for dependency injection and testability
 */
#pragma once
#include <cstdint>
#include <cstddef>

namespace interfaces {

enum class Resolution : uint8_t {
    QQVGA = 0,  // 160x120
    QVGA = 1,   // 320x240
    VGA = 2,    // 640x480
    SVGA = 3,   // 800x600
    XGA = 4,    // 1024x768
    HD = 5,     // 1280x720
    SXGA = 6,   // 1280x1024
    UXGA = 7    // 1600x1200
};

// Immutable view of a captured frame
struct FrameView {
    const uint8_t* data = nullptr;
    size_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t timestamp_us = 0;
    
    bool valid() const { return data != nullptr && size > 0; }
};

struct CameraConfig {
    Resolution resolution = Resolution::VGA;
    uint8_t jpeg_quality = 20;        // 10-63 (lower = better quality, larger files)
    uint8_t frame_buffer_count = 2;   // Number of frame buffers in DMA
};

/**
 * @brief Abstract camera interface
 * 
 * Allows swapping real camera with mock for testing.
 * Thread-safety: Implementation should handle concurrent access.
 */
class ICamera {
public:
    virtual ~ICamera() = default;
    
    // Lifecycle
    virtual bool init(const CameraConfig& config) = 0;
    virtual void deinit() = 0;
    virtual bool is_initialized() const = 0;
    
    // Frame capture
    virtual FrameView capture_frame() = 0;
    virtual void release_frame() = 0;
    
    // Configuration (can be changed at runtime)
    virtual bool set_resolution(Resolution res) = 0;
    virtual bool set_quality(uint8_t quality) = 0;
    virtual Resolution get_resolution() const = 0;
    virtual uint8_t get_quality() const = 0;
};

} // namespace interfaces
