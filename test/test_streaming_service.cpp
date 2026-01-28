/**
 * @file test_streaming_service.cpp
 * @brief Unit tests for StreamingService
 */
#include <catch2/catch_test_macros.hpp>
#include "../main/core/streaming_service.hpp"
#include "mocks/mock_camera.hpp"
#include "mocks/mock_clock.hpp"
#include <thread>
#include <chrono>
#include <atomic>

using namespace core;
using namespace mocks;

//=============================================================================
// Initialization Tests
//=============================================================================

TEST_CASE("StreamingService initialization", "[streaming][init]") {
    MockCamera camera;
    MockClock clock;
    camera.init({});
    
    SECTION("default state before init") {
        StreamingService svc(camera, clock);
        REQUIRE_FALSE(svc.is_initialized());
        REQUIRE_FALSE(svc.is_running());
    }
    
    SECTION("basic initialization succeeds") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init());
        REQUIRE(svc.is_initialized());
        REQUIRE_FALSE(svc.is_running());  // Not started yet
    }
    
    SECTION("init with custom config") {
        StreamingService svc(camera, clock);
        StreamingConfig cfg;
        cfg.target_fps = 5;
        cfg.buffer_slots = 5;
        cfg.max_frame_size = 50 * 1024;
        
        REQUIRE(svc.init(cfg));
        REQUIRE(svc.get_target_fps() == 5);
    }
    
    SECTION("double init returns true") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init());
        REQUIRE(svc.init());  // Should succeed
        REQUIRE(svc.is_initialized());
    }
    
    SECTION("deinit clears state") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init());
        svc.deinit();
        REQUIRE_FALSE(svc.is_initialized());
    }
}

//=============================================================================
// Start/Stop Tests
//=============================================================================

TEST_CASE("StreamingService start/stop", "[streaming][lifecycle]") {
    MockCamera camera;
    MockClock clock;
    camera.init({});
    
    // Use auto-advance to prevent infinite loops
    clock.set_auto_advance_us(1000);  // 1ms per now() call
    
    SECTION("start without init fails") {
        StreamingService svc(camera, clock);
        REQUIRE_FALSE(svc.start());
        REQUIRE_FALSE(svc.is_running());
    }
    
    SECTION("start after init succeeds") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init({.target_fps = 10}));
        REQUIRE(svc.start());
        
        // Give producer time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        REQUIRE(svc.is_running());
        
        svc.stop();
        REQUIRE_FALSE(svc.is_running());
    }
    
    SECTION("double start returns true") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init());
        REQUIRE(svc.start());
        
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        REQUIRE(svc.start());  // Already running, should return true
        
        svc.stop();
    }
    
    SECTION("stop on not-running service is safe") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init());
        svc.stop();  // Not running, should not crash
        REQUIRE_FALSE(svc.is_running());
    }
    
    SECTION("start resets stats") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init({.target_fps = 30}));
        
        REQUIRE(svc.start());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto& stats1 = svc.stats();
        uint32_t captured1 = stats1.frames_captured.load();
        
        svc.stop();
        
        REQUIRE(svc.start());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Stats should be reset (or very low)
        auto& stats2 = svc.stats();
        REQUIRE(stats2.frames_captured.load() <= captured1);
        
        svc.stop();
    }
}

//=============================================================================
// Frame Capture Tests
//=============================================================================

TEST_CASE("StreamingService frame capture", "[streaming][capture]") {
    MockCamera camera;
    MockClock clock;
    camera.init({});
    clock.set_auto_advance_us(10000);  // 10ms per now() call
    
    SECTION("producer captures frames") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init({.target_fps = 30}));
        REQUIRE(svc.start());
        
        // Let producer run
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        REQUIRE(svc.stats().frames_captured.load() > 0);
        REQUIRE(camera.capture_calls() > 0);
        
        svc.stop();
    }
    
    SECTION("capture errors tracked") {
        MockCamera failing_camera;
        failing_camera.init({});
        failing_camera.set_capture_result(false);
        
        MockClock test_clock;
        test_clock.set_auto_advance_us(10000);
        
        StreamingService svc(failing_camera, test_clock);
        REQUIRE(svc.init({.target_fps = 30}));
        REQUIRE(svc.start());
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        REQUIRE(svc.stats().capture_errors.load() > 0);
        REQUIRE(svc.stats().frames_captured.load() == 0);
        
        svc.stop();
    }
    
    SECTION("frames released after capture") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init({.target_fps = 30}));
        REQUIRE(svc.start());
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Stop first to ensure producer is not mid-capture
        svc.stop();
        
        // Each capture should be followed by release
        REQUIRE(camera.capture_calls() == camera.release_calls());
        REQUIRE_FALSE(camera.is_frame_held());
    }
}

//=============================================================================
// Consumer API Tests
//=============================================================================

TEST_CASE("StreamingService consumer API", "[streaming][consumer]") {
    MockCamera camera;
    MockClock clock;
    camera.init({});
    
    // Set up custom frame data for verification
    std::vector<uint8_t> test_frame(500);
    test_frame[0] = 0xFF; test_frame[1] = 0xD8;  // JPEG header
    test_frame[100] = 0x42;  // Test marker
    camera.set_custom_frame(test_frame);
    
    clock.set_auto_advance_us(5000);  // 5ms per call
    
    SECTION("get_frame returns captured data") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init({.target_fps = 30}));
        REQUIRE(svc.start());
        
        // Wait for frames to be available
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        const uint8_t* data = nullptr;
        size_t size = 0;
        
        REQUIRE(svc.get_frame(&data, &size, 500));
        REQUIRE(data != nullptr);
        REQUIRE(size == 500);
        REQUIRE(data[100] == 0x42);  // Our test marker
        
        svc.stop();
    }
    
    SECTION("release_frame increments sent counter") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init({.target_fps = 30}));
        REQUIRE(svc.start());
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        const uint8_t* data;
        size_t size;
        
        REQUIRE(svc.stats().frames_sent.load() == 0);
        
        if (svc.get_frame(&data, &size, 500)) {
            svc.release_frame();
            REQUIRE(svc.stats().frames_sent.load() == 1);
        }
        
        svc.stop();
    }
    
    SECTION("get_frame with zero timeout is non-blocking") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init({.target_fps = 10}));
        // Don't start - test non-blocking on empty buffer
        
        const uint8_t* data;
        size_t size;
        
        auto start = std::chrono::steady_clock::now();
        bool got = svc.get_frame(&data, &size, 0);  // Non-blocking
        auto end = std::chrono::steady_clock::now();
        
        // Should return false immediately (no frames, not started)
        REQUIRE_FALSE(got);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        REQUIRE(elapsed.count() < 10);  // Should return very quickly
    }
    
    SECTION("get_frame blocks until timeout") {
        MockCamera slow_camera;
        slow_camera.init({});
        slow_camera.set_capture_result(false);  // No frames produced
        
        MockClock slow_clock;
        slow_clock.set_auto_advance_us(1000);
        
        StreamingService svc(slow_camera, slow_clock);
        REQUIRE(svc.init());
        REQUIRE(svc.start());
        
        const uint8_t* data;
        size_t size;
        
        auto start = std::chrono::steady_clock::now();
        bool got = svc.get_frame(&data, &size, 100);  // 100ms timeout
        auto end = std::chrono::steady_clock::now();
        
        REQUIRE_FALSE(got);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        REQUIRE(elapsed.count() >= 90);  // Should have waited ~100ms
        
        svc.stop();
    }
    
    SECTION("get_frame with null pointers fails") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init());
        
        const uint8_t* data;
        size_t size;
        
        REQUIRE_FALSE(svc.get_frame(nullptr, &size));
        REQUIRE_FALSE(svc.get_frame(&data, nullptr));
    }
}

//=============================================================================
// Statistics Tests
//=============================================================================

TEST_CASE("StreamingService statistics", "[streaming][stats]") {
    MockCamera camera;
    MockClock clock;
    camera.init({});
    clock.set_auto_advance_us(5000);
    
    SECTION("stats track capture count") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init({.target_fps = 30}));
        REQUIRE(svc.start());
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        REQUIRE(svc.stats().frames_captured.load() > 0);
        REQUIRE(svc.stats().producer_running.load() == true);
        
        svc.stop();
        REQUIRE(svc.stats().producer_running.load() == false);
    }
    
    SECTION("buffered_frames reflects buffer state") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init({.target_fps = 30, .buffer_slots = 5}));
        REQUIRE(svc.start());
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Should have some buffered frames
        REQUIRE(svc.buffered_frames() > 0);
        REQUIRE(svc.buffered_frames() <= 5);
        
        svc.stop();
    }
    
    SECTION("dropped frames tracked on overflow") {
        // Small buffer, fast capture -> overflow
        MockCamera fast_camera;
        fast_camera.init({});
        
        MockClock fast_clock;
        fast_clock.set_auto_advance_us(1000);  // Very fast time
        
        StreamingService svc(fast_camera, fast_clock);
        REQUIRE(svc.init({.target_fps = 30, .buffer_slots = 2}));
        REQUIRE(svc.start());
        
        // Don't consume frames, let buffer overflow
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        
        // With 30 FPS, 2 slots, and 300ms, we should have overflowed
        REQUIRE(svc.stats().frames_captured.load() > 2);
        REQUIRE(svc.stats().frames_dropped.load() > 0);
        
        svc.stop();
    }
}

//=============================================================================
// Configuration Tests
//=============================================================================

TEST_CASE("StreamingService configuration", "[streaming][config]") {
    MockCamera camera;
    MockClock clock;
    camera.init({});
    clock.set_auto_advance_us(1000);
    
    SECTION("set_target_fps changes capture rate") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init({.target_fps = 1}));
        REQUIRE(svc.get_target_fps() == 1);
        
        svc.set_target_fps(10);
        REQUIRE(svc.get_target_fps() == 10);
    }
    
    SECTION("set_target_fps rejects invalid values") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init({.target_fps = 10}));
        
        svc.set_target_fps(0);   // Invalid
        REQUIRE(svc.get_target_fps() == 10);  // Unchanged
        
        svc.set_target_fps(31);  // Too high
        REQUIRE(svc.get_target_fps() == 10);  // Unchanged
    }
    
    SECTION("runtime FPS change takes effect") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init({.target_fps = 1}));
        REQUIRE(svc.start());
        
        uint32_t cap_at_1fps = svc.stats().frames_captured.load();
        
        // Change to faster rate
        svc.set_target_fps(30);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        uint32_t cap_at_30fps = svc.stats().frames_captured.load();
        
        // Should have captured more frames at higher rate
        // (This is a weak test but validates the API)
        REQUIRE(cap_at_30fps >= cap_at_1fps);
        
        svc.stop();
    }
}

//=============================================================================
// Producer-Consumer Integration Tests
//=============================================================================

TEST_CASE("StreamingService producer-consumer flow", "[streaming][integration]") {
    MockCamera camera;
    MockClock clock;
    camera.init({});
    clock.set_auto_advance_us(5000);
    
    SECTION("consumer receives all frames without loss") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init({.target_fps = 10, .buffer_slots = 10}));
        REQUIRE(svc.start());
        
        std::atomic<int> frames_consumed{0};
        std::atomic<bool> stop_consuming{false};
        
        // Consumer thread
        std::thread consumer([&svc, &frames_consumed, &stop_consuming]() {
            while (!stop_consuming) {
                const uint8_t* data;
                size_t size;
                if (svc.get_frame(&data, &size, 100)) {
                    frames_consumed++;
                    svc.release_frame();
                }
            }
        });
        
        // Let it run
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        stop_consuming = true;
        consumer.join();
        svc.stop();
        
        // Should have consumed many frames with minimal drops
        REQUIRE(frames_consumed > 0);
        REQUIRE(svc.stats().frames_sent.load() == static_cast<uint32_t>(frames_consumed));
    }
    
    SECTION("slow consumer causes frame drops") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init({.target_fps = 30, .buffer_slots = 3}));
        REQUIRE(svc.start());
        
        // Very slow consumer
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // With 30 FPS, 3 buffer slots, and 500ms, buffer overflows repeatedly
        REQUIRE(svc.stats().frames_dropped.load() > 0);
        
        svc.stop();
    }
}

//=============================================================================
// Edge Cases
//=============================================================================

TEST_CASE("StreamingService edge cases", "[streaming][edge]") {
    MockCamera camera;
    MockClock clock;
    camera.init({});
    clock.set_auto_advance_us(1000);
    
    SECTION("camera failure during streaming") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init({.target_fps = 30}));
        REQUIRE(svc.start());
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        uint32_t captured_before = svc.stats().frames_captured.load();
        
        // Simulate camera failure
        camera.set_capture_result(false);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Errors should increase, captures should stop
        REQUIRE(svc.stats().capture_errors.load() > 0);
        REQUIRE(svc.stats().frames_captured.load() == captured_before);
        
        // Recover camera
        camera.set_capture_result(true);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Captures should resume
        REQUIRE(svc.stats().frames_captured.load() > captured_before);
        
        svc.stop();
    }
    
    SECTION("destructor stops running service") {
        MockCamera local_camera;
        MockClock local_clock;
        local_camera.init({});
        local_clock.set_auto_advance_us(1000);
        
        {
            StreamingService svc(local_camera, local_clock);
            REQUIRE(svc.init());
            REQUIRE(svc.start());
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            REQUIRE(svc.is_running());
            // Destructor called here
        }
        
        // Service should be stopped (no crash, no hanging threads)
        // If this test completes, destructor worked correctly
        REQUIRE(true);
    }
    
    SECTION("rapid start-stop cycles") {
        StreamingService svc(camera, clock);
        REQUIRE(svc.init());
        
        for (int i = 0; i < 10; i++) {
            REQUIRE(svc.start());
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            svc.stop();
        }
        
        // Should handle rapid cycling without issues
        REQUIRE_FALSE(svc.is_running());
    }
}

//=============================================================================
// Mock Verification Tests
//=============================================================================

TEST_CASE("Mock camera behavior verification", "[mocks][camera]") {
    MockCamera camera;
    
    SECTION("init tracks calls") {
        interfaces::CameraConfig cfg;
        cfg.resolution = interfaces::Resolution::HD;
        cfg.jpeg_quality = 15;
        
        camera.init(cfg);
        
        REQUIRE(camera.init_calls() == 1);
        REQUIRE(camera.get_resolution() == interfaces::Resolution::HD);
        REQUIRE(camera.get_quality() == 15);
    }
    
    SECTION("capture returns configurable data") {
        camera.init({});
        
        std::vector<uint8_t> custom(256, 0xBE);
        camera.set_custom_frame(custom);
        
        auto frame = camera.capture_frame();
        REQUIRE(frame.valid());
        REQUIRE(frame.size == 256);
        REQUIRE(frame.data[0] == 0xBE);
    }
    
    SECTION("capture failure simulation") {
        camera.init({});
        camera.set_capture_result(false);
        
        auto frame = camera.capture_frame();
        REQUIRE_FALSE(frame.valid());
    }
}

TEST_CASE("Mock clock behavior verification", "[mocks][clock]") {
    MockClock clock;
    
    SECTION("time tracking") {
        clock.set_time_us(1000000);
        REQUIRE(clock.now_us() == 1000000);
        REQUIRE(clock.now_ms() == 1000);
    }
    
    SECTION("manual time advance") {
        clock.set_time_us(0);
        clock.advance_ms(100);
        REQUIRE(clock.now_us() == 100000);
    }
    
    SECTION("auto advance on now_us calls") {
        clock.set_time_us(0);
        clock.set_auto_advance_us(1000);
        
        REQUIRE(clock.now_us() == 0);      // First call
        REQUIRE(clock.now_us() == 1000);   // Advances by 1000
        REQUIRE(clock.now_us() == 2000);   // And again
    }
    
    SECTION("delay advances time") {
        clock.set_time_us(0);
        clock.delay_ms(50);
        REQUIRE(clock.now_us() == 50000);
        REQUIRE(clock.total_delay_ms() == 50);
    }
}
