/**
 * @file test_frame_buffer.cpp
 * @brief Unit tests for FrameBuffer
 */
#include <catch2/catch_test_macros.hpp>
#include "../main/core/frame_buffer.hpp"
#include <thread>
#include <vector>
#include <atomic>

using namespace core;

// Helper to create test data
static std::vector<uint8_t> make_test_frame(size_t size, uint8_t fill = 0xAA) {
    std::vector<uint8_t> data(size, fill);
    // Add JPEG-like markers
    if (size >= 4) {
        data[0] = 0xFF; data[1] = 0xD8;  // SOI
        data[size-2] = 0xFF; data[size-1] = 0xD9;  // EOI
    }
    return data;
}

//=============================================================================
// Initialization Tests
//=============================================================================

TEST_CASE("FrameBuffer initialization", "[frame_buffer][init]") {
    FrameBuffer buffer;
    
    SECTION("default state before init") {
        REQUIRE_FALSE(buffer.is_initialized());
        REQUIRE(buffer.available() == 0);
        REQUIRE(buffer.empty());
        REQUIRE_FALSE(buffer.full());
    }
    
    SECTION("basic initialization succeeds") {
        REQUIRE(buffer.init(3, 1024, false));
        REQUIRE(buffer.is_initialized());
        REQUIRE(buffer.capacity() == 3);
        REQUIRE(buffer.max_frame_size() == 1024);
        REQUIRE(buffer.empty());
    }
    
    SECTION("double init returns true") {
        REQUIRE(buffer.init(3, 1024, false));
        REQUIRE(buffer.init(3, 1024, false));  // Should succeed (already initialized)
        REQUIRE(buffer.is_initialized());
    }
    
    SECTION("init with zero slots fails") {
        REQUIRE_FALSE(buffer.init(0, 1024, false));
        REQUIRE_FALSE(buffer.is_initialized());
    }
    
    SECTION("init with zero frame size fails") {
        REQUIRE_FALSE(buffer.init(3, 0, false));
        REQUIRE_FALSE(buffer.is_initialized());
    }
    
    SECTION("deinit clears state") {
        REQUIRE(buffer.init(3, 1024, false));
        buffer.deinit();
        REQUIRE_FALSE(buffer.is_initialized());
    }
    
    SECTION("deinit on uninitialized buffer is safe") {
        buffer.deinit();  // Should not crash
        REQUIRE_FALSE(buffer.is_initialized());
    }
}

//=============================================================================
// Push/Peek/Pop Tests
//=============================================================================

TEST_CASE("FrameBuffer push operations", "[frame_buffer][push]") {
    FrameBuffer buffer;
    REQUIRE(buffer.init(3, 4096, false));
    
    auto frame1 = make_test_frame(100, 0x11);
    auto frame2 = make_test_frame(200, 0x22);
    auto frame3 = make_test_frame(300, 0x33);
    
    SECTION("push single frame") {
        REQUIRE(buffer.push(frame1.data(), frame1.size(), 1000));
        REQUIRE(buffer.available() == 1);
        REQUIRE_FALSE(buffer.empty());
        REQUIRE_FALSE(buffer.full());
    }
    
    SECTION("push until full") {
        REQUIRE(buffer.push(frame1.data(), frame1.size(), 1000));
        REQUIRE(buffer.push(frame2.data(), frame2.size(), 2000));
        REQUIRE(buffer.push(frame3.data(), frame3.size(), 3000));
        
        REQUIRE(buffer.available() == 3);
        REQUIRE(buffer.full());
    }
    
    SECTION("push null data fails") {
        REQUIRE_FALSE(buffer.push(nullptr, 100, 0));
        REQUIRE(buffer.empty());
    }
    
    SECTION("push zero size fails") {
        REQUIRE_FALSE(buffer.push(frame1.data(), 0, 0));
        REQUIRE(buffer.empty());
    }
    
    SECTION("push oversized frame fails") {
        auto huge = make_test_frame(5000);  // > 4096 max
        REQUIRE_FALSE(buffer.push(huge.data(), huge.size(), 0));
        REQUIRE(buffer.empty());
    }
    
    SECTION("push on uninitialized buffer fails") {
        FrameBuffer uninit;
        REQUIRE_FALSE(uninit.push(frame1.data(), frame1.size(), 0));
    }
}

TEST_CASE("FrameBuffer peek operations", "[frame_buffer][peek]") {
    FrameBuffer buffer;
    REQUIRE(buffer.init(3, 4096, false));
    
    auto frame1 = make_test_frame(100, 0x11);
    auto frame2 = make_test_frame(200, 0x22);
    
    SECTION("peek empty buffer returns false") {
        const uint8_t* data = nullptr;
        size_t size = 0;
        REQUIRE_FALSE(buffer.peek(&data, &size));
    }
    
    SECTION("peek returns oldest frame") {
        REQUIRE(buffer.push(frame1.data(), frame1.size(), 1000));
        REQUIRE(buffer.push(frame2.data(), frame2.size(), 2000));
        
        const uint8_t* data = nullptr;
        size_t size = 0;
        int64_t ts = 0;
        
        REQUIRE(buffer.peek(&data, &size, &ts));
        REQUIRE(size == 100);  // First frame
        REQUIRE(ts == 1000);
        REQUIRE(data[2] == 0x11);  // Check fill byte (skip JPEG header)
    }
    
    SECTION("peek does not remove frame") {
        REQUIRE(buffer.push(frame1.data(), frame1.size(), 1000));
        
        const uint8_t* data = nullptr;
        size_t size = 0;
        
        REQUIRE(buffer.peek(&data, &size));
        REQUIRE(buffer.available() == 1);  // Still there
        
        REQUIRE(buffer.peek(&data, &size));  // Can peek again
        REQUIRE(buffer.available() == 1);
    }
    
    SECTION("peek with null pointers fails") {
        REQUIRE(buffer.push(frame1.data(), frame1.size(), 0));
        
        const uint8_t* data = nullptr;
        size_t size = 0;
        
        REQUIRE_FALSE(buffer.peek(nullptr, &size));
        REQUIRE_FALSE(buffer.peek(&data, nullptr));
    }
}

TEST_CASE("FrameBuffer pop operations", "[frame_buffer][pop]") {
    FrameBuffer buffer;
    REQUIRE(buffer.init(3, 4096, false));
    
    auto frame1 = make_test_frame(100, 0x11);
    auto frame2 = make_test_frame(200, 0x22);
    auto frame3 = make_test_frame(300, 0x33);
    
    SECTION("pop removes oldest frame") {
        REQUIRE(buffer.push(frame1.data(), frame1.size(), 1000));
        REQUIRE(buffer.push(frame2.data(), frame2.size(), 2000));
        
        buffer.pop();
        REQUIRE(buffer.available() == 1);
        
        const uint8_t* data = nullptr;
        size_t size = 0;
        int64_t ts = 0;
        
        REQUIRE(buffer.peek(&data, &size, &ts));
        REQUIRE(size == 200);  // Second frame now oldest
        REQUIRE(ts == 2000);
    }
    
    SECTION("pop on empty buffer is safe") {
        buffer.pop();  // Should not crash
        REQUIRE(buffer.empty());
    }
    
    SECTION("FIFO order maintained") {
        REQUIRE(buffer.push(frame1.data(), frame1.size(), 1000));
        REQUIRE(buffer.push(frame2.data(), frame2.size(), 2000));
        REQUIRE(buffer.push(frame3.data(), frame3.size(), 3000));
        
        const uint8_t* data = nullptr;
        size_t size = 0;
        
        // First pop
        REQUIRE(buffer.peek(&data, &size));
        REQUIRE(size == 100);
        buffer.pop();
        
        // Second pop
        REQUIRE(buffer.peek(&data, &size));
        REQUIRE(size == 200);
        buffer.pop();
        
        // Third pop
        REQUIRE(buffer.peek(&data, &size));
        REQUIRE(size == 300);
        buffer.pop();
        
        REQUIRE(buffer.empty());
    }
}

//=============================================================================
// Overflow Tests
//=============================================================================

TEST_CASE("FrameBuffer overflow handling", "[frame_buffer][overflow]") {
    FrameBuffer buffer;
    REQUIRE(buffer.init(3, 4096, false));
    
    auto frame1 = make_test_frame(100, 0x11);
    auto frame2 = make_test_frame(200, 0x22);
    auto frame3 = make_test_frame(300, 0x33);
    auto frame4 = make_test_frame(400, 0x44);
    
    SECTION("overflow drops oldest frame") {
        REQUIRE(buffer.push(frame1.data(), frame1.size(), 1000));
        REQUIRE(buffer.push(frame2.data(), frame2.size(), 2000));
        REQUIRE(buffer.push(frame3.data(), frame3.size(), 3000));
        REQUIRE(buffer.full());
        REQUIRE(buffer.frames_dropped() == 0);
        
        // Push fourth frame - should drop oldest (frame1)
        REQUIRE(buffer.push(frame4.data(), frame4.size(), 4000));
        
        REQUIRE(buffer.available() == 3);
        REQUIRE(buffer.frames_dropped() == 1);
        
        // Oldest should now be frame2
        const uint8_t* data = nullptr;
        size_t size = 0;
        int64_t ts = 0;
        
        REQUIRE(buffer.peek(&data, &size, &ts));
        REQUIRE(size == 200);  // frame2
        REQUIRE(ts == 2000);
    }
    
    SECTION("multiple overflows tracked") {
        // Fill buffer
        REQUIRE(buffer.push(frame1.data(), frame1.size(), 1000));
        REQUIRE(buffer.push(frame2.data(), frame2.size(), 2000));
        REQUIRE(buffer.push(frame3.data(), frame3.size(), 3000));
        
        // Overflow 5 times
        for (int i = 0; i < 5; i++) {
            REQUIRE(buffer.push(frame4.data(), frame4.size(), 4000 + i));
        }
        
        REQUIRE(buffer.frames_dropped() == 5);
        REQUIRE(buffer.available() == 3);
    }
    
    SECTION("reset_stats clears dropped counter") {
        REQUIRE(buffer.push(frame1.data(), frame1.size(), 1000));
        REQUIRE(buffer.push(frame2.data(), frame2.size(), 2000));
        REQUIRE(buffer.push(frame3.data(), frame3.size(), 3000));
        REQUIRE(buffer.push(frame4.data(), frame4.size(), 4000));
        
        REQUIRE(buffer.frames_dropped() == 1);
        buffer.reset_stats();
        REQUIRE(buffer.frames_dropped() == 0);
    }
}

//=============================================================================
// Clear Tests
//=============================================================================

TEST_CASE("FrameBuffer clear operation", "[frame_buffer][clear]") {
    FrameBuffer buffer;
    REQUIRE(buffer.init(3, 4096, false));
    
    auto frame = make_test_frame(100);
    
    SECTION("clear empties buffer") {
        REQUIRE(buffer.push(frame.data(), frame.size(), 1000));
        REQUIRE(buffer.push(frame.data(), frame.size(), 2000));
        REQUIRE_FALSE(buffer.empty());
        
        buffer.clear();
        REQUIRE(buffer.empty());
        REQUIRE(buffer.available() == 0);
    }
    
    SECTION("clear preserves dropped counter") {
        REQUIRE(buffer.push(frame.data(), frame.size(), 1));
        REQUIRE(buffer.push(frame.data(), frame.size(), 2));
        REQUIRE(buffer.push(frame.data(), frame.size(), 3));
        REQUIRE(buffer.push(frame.data(), frame.size(), 4));  // Overflow
        
        REQUIRE(buffer.frames_dropped() == 1);
        buffer.clear();
        REQUIRE(buffer.frames_dropped() == 1);  // Counter preserved
    }
    
    SECTION("clear on empty buffer is safe") {
        buffer.clear();
        REQUIRE(buffer.empty());
    }
    
    SECTION("buffer usable after clear") {
        REQUIRE(buffer.push(frame.data(), frame.size(), 1000));
        buffer.clear();
        
        REQUIRE(buffer.push(frame.data(), frame.size(), 2000));
        REQUIRE(buffer.available() == 1);
    }
}

//=============================================================================
// Thread Safety Tests
//=============================================================================

TEST_CASE("FrameBuffer thread safety", "[frame_buffer][threading]") {
    FrameBuffer buffer;
    REQUIRE(buffer.init(10, 1024, false));
    
    SECTION("concurrent push from multiple threads") {
        std::atomic<int> successful_pushes{0};
        const int num_threads = 4;
        const int pushes_per_thread = 100;
        
        std::vector<std::thread> threads;
        
        for (int t = 0; t < num_threads; t++) {
            threads.emplace_back([&buffer, &successful_pushes, t]() {
                auto frame = make_test_frame(100, static_cast<uint8_t>(t));
                for (int i = 0; i < 100; i++) {
                    if (buffer.push(frame.data(), frame.size(), t * 1000 + i)) {
                        successful_pushes++;
                    }
                }
            });
        }
        
        for (auto& th : threads) {
            th.join();
        }
        
        // All pushes should succeed (overflow drops are still successful pushes)
        REQUIRE(successful_pushes == num_threads * pushes_per_thread);
    }
    
    SECTION("concurrent push and pop") {
        std::atomic<bool> stop{false};
        std::atomic<int> frames_read{0};
        
        // Producer thread
        std::thread producer([&buffer, &stop]() {
            auto frame = make_test_frame(100);
            for (int i = 0; i < 1000 && !stop; i++) {
                buffer.push(frame.data(), frame.size(), i);
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
        
        // Consumer thread
        std::thread consumer([&buffer, &stop, &frames_read]() {
            const uint8_t* data;
            size_t size;
            while (!stop || !buffer.empty()) {
                if (buffer.peek(&data, &size)) {
                    buffer.pop();
                    frames_read++;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(5));
            }
        });
        
        // Let it run
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        stop = true;
        
        producer.join();
        consumer.join();
        
        // Should have read some frames without crashing
        REQUIRE(frames_read > 0);
    }
}

//=============================================================================
// Edge Cases
//=============================================================================

TEST_CASE("FrameBuffer edge cases", "[frame_buffer][edge]") {
    SECTION("single slot buffer works correctly") {
        FrameBuffer buffer;
        REQUIRE(buffer.init(1, 1024, false));
        
        auto frame1 = make_test_frame(100, 0x11);
        auto frame2 = make_test_frame(200, 0x22);
        
        REQUIRE(buffer.push(frame1.data(), frame1.size(), 1000));
        REQUIRE(buffer.full());
        
        // Overflow immediately
        REQUIRE(buffer.push(frame2.data(), frame2.size(), 2000));
        REQUIRE(buffer.frames_dropped() == 1);
        
        const uint8_t* data;
        size_t size;
        REQUIRE(buffer.peek(&data, &size));
        REQUIRE(size == 200);  // Latest frame
    }
    
    SECTION("exactly max size frame") {
        FrameBuffer buffer;
        REQUIRE(buffer.init(2, 1024, false));
        
        auto frame = make_test_frame(1024);  // Exactly max
        REQUIRE(buffer.push(frame.data(), frame.size(), 0));
        REQUIRE(buffer.available() == 1);
    }
    
    SECTION("frame size just over max fails") {
        FrameBuffer buffer;
        REQUIRE(buffer.init(2, 1024, false));
        
        auto frame = make_test_frame(1025);  // One byte over
        REQUIRE_FALSE(buffer.push(frame.data(), frame.size(), 0));
    }
    
    SECTION("timestamp preserved correctly") {
        FrameBuffer buffer;
        REQUIRE(buffer.init(3, 1024, false));
        
        auto frame = make_test_frame(100);
        int64_t test_ts = 123456789012345LL;  // Large timestamp
        
        REQUIRE(buffer.push(frame.data(), frame.size(), test_ts));
        
        const uint8_t* data;
        size_t size;
        int64_t ts;
        REQUIRE(buffer.peek(&data, &size, &ts));
        REQUIRE(ts == test_ts);
    }
}
