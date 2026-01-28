# Project 4: WiFi Camera (Testable Architecture)

ESP32-S3 camera streaming with dependency injection for testability and producer-consumer buffering for smooth frame rate.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        Dependencies                              │
│  ┌─────────────┐    ┌──────────────┐                            │
│  │  ICamera    │    │   IClock     │  ← Abstract interfaces     │
│  └──────┬──────┘    └──────┬───────┘                            │
│         │                  │                                     │
│    ┌────┴────┐        ┌────┴────┐                               │
│    │  Mock   │        │  Mock   │  ← Test implementations       │
│    │ Camera  │        │  Clock  │                               │
│    └─────────┘        └─────────┘                               │
│    ┌────┴────┐        ┌────┴────┐                               │
│    │   ESP   │        │   ESP   │  ← Production implementations │
│    │ Camera  │        │  Clock  │                               │
│    └─────────┘        └─────────┘                               │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                    Streaming Pipeline                            │
│                                                                  │
│  [Camera] → [Producer Task] → [FrameBuffer] → [HTTP] → [Browser]│
│              (3 FPS fixed)    (3 slots)       (MJPEG)           │
│                    ↓               ↓                             │
│              Captures at      Absorbs timing                     │
│              fixed rate       variations                         │
└─────────────────────────────────────────────────────────────────┘
```

## Key Design Decisions

### 1. Dependency Injection

All hardware interactions go through abstract interfaces:
- `ICamera` - Camera operations (capture, release, config)
- `IClock` - Time and delay operations

**Why:** Allows swapping real hardware with mocks for unit testing. Production code uses `EspCameraDriver` and `EspClockDriver`. Tests use `MockCamera` and `MockClock`.

### 2. Producer-Consumer Buffering

**Problem:** Direct camera-to-network streaming has variable timing:
- Camera capture: 50-200ms (varies with lighting, resolution)
- Network send: 10-500ms (varies with WiFi conditions)
- Result: Stuttery, unpredictable frame rate

**Solution:** Fixed-rate capture with circular buffer:
- Producer task captures at fixed interval (333ms for 3 FPS)
- `FrameBuffer` holds 3 pre-allocated frames
- Consumer (HTTP handler) blocks until frame available
- Buffer absorbs timing variations
- Overflow drops oldest frame (freshness > history)

### 3. FPS Counter Removed

**Problem:** Client-side FPS calculation was unreliable (always showed 0.0).

**Solution:** Removed entirely. Frame counters (captured/sent/dropped/buffered) provide more useful debugging information. Visual smoothness is the actual goal, not a numeric FPS display.

## File Structure

```
project-04-wifi-camera-v2/
├── CMakeLists.txt              # Dual-build: ESP-IDF or host tests
├── sdkconfig.defaults
├── partitions.csv
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml       # esp32-camera, mdns
│   ├── Kconfig.projbuild
│   ├── main.cpp                # Wires dependencies, starts services
│   ├── interfaces/
│   │   ├── i_camera.hpp        # Camera abstraction
│   │   └── i_clock.hpp         # Time abstraction
│   ├── drivers/
│   │   ├── esp_camera_driver.hpp   # Production: wraps esp_camera
│   │   └── esp_clock_driver.hpp    # Production: wraps esp_timer
│   └── core/
│       ├── frame_buffer.hpp    # Thread-safe ring buffer
│       ├── streaming_service.hpp   # Producer-consumer pipeline
│       ├── web_server.hpp      # HTTP + MJPEG streaming
│       └── wifi_manager.hpp    # WiFi connection
└── test/
    ├── test_main.cpp           # Catch2 entry point
    ├── test_frame_buffer.cpp   # FrameBuffer unit tests
    ├── test_streaming_service.cpp  # StreamingService tests
    └── mocks/
        ├── mock_camera.hpp     # Configurable fake camera
        └── mock_clock.hpp      # Controllable fake clock
```

## Building

### ESP32 Target

```bash
cd project-04-wifi-camera-v2
idf.py set-target esp32s3
idf.py menuconfig  # Configure WiFi credentials
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Host Tests (macOS)

Prerequisites:
```bash
# Install CMake if not present
brew install cmake lcov
```

Build and run tests:
```bash
cd project-04-wifi-camera-v2

# Create build directory (name doesn't matter, just not 'build')
mkdir build-host-tests
cd build-host-tests

# Configure with BUILD_TESTS=ON
# This works even if IDF_PATH is set - BUILD_TESTS takes priority
cmake -DBUILD_TESTS=ON ..

# Build (this will download Catch2 automatically)
cmake --build . -j

# Run tests via CTest
ctest --output-on-failure

# Or run directly with detailed output
./wifi_camera_tests

# For even more detail:
./wifi_camera_tests --reporter console --success
```

### Host Tests (Linux/Ubuntu)

Prerequisites:
```bash
sudo apt-get install cmake build-essential lcov
```

Build and run (same as macOS):
```bash
cd project-04-wifi-camera-v2
mkdir build-host-tests
cd build-host-tests
cmake -DBUILD_TESTS=ON ..
cmake --build .
ctest --output-on-failure
```

### Coverage Report

```bash
cd project-04-wifi-camera-v2
mkdir build-coverage
cd build-coverage

# Configure with coverage enabled
cmake -DBUILD_TESTS=ON -DCOVERAGE=ON ..

# Build
cmake --build .

# Run tests and generate coverage report
cmake --build . --target coverage

# Open report (macOS)
open coverage_report/index.html

# Open report (Linux)
xdg-open coverage_report/index.html
```

**Note:** If you have ESP-IDF sourced in your shell (`source $IDF_PATH/export.sh`), 
you may need to unset IDF_PATH or use a fresh terminal for host tests:
```bash
unset IDF_PATH
cmake -DBUILD_TESTS=ON ..
```

## Test Coverage

Tests cover:
- **FrameBuffer:** Init, push, peek, pop, overflow, threading, edge cases
- **StreamingService:** Init, start/stop, capture, consumer API, stats, config
- **Mocks:** MockCamera and MockClock behavior verification

Target: >80% coverage (with full real hardware code excluded).

## Configuration

| Setting | Default | Description |
|---------|---------|-------------|
| `CONFIG_STREAM_FPS` | 3 | Target frame rate (1-15) |
| `CONFIG_WIFI_SSID` | - | WiFi network name |
| `CONFIG_WIFI_PASSWORD` | - | WiFi password |
| `CONFIG_MDNS_HOSTNAME` | espcam | Access via espcam.local |

## Memory Usage

| Component | Location | Size |
|-----------|----------|------|
| Frame buffers (3×100KB) | PSRAM | ~300KB |
| Camera driver buffers | PSRAM | ~150KB |
| WiFi stack | DRAM | ~40KB |
| HTTP server | DRAM | ~8KB |
| Streaming service | DRAM | ~4KB |

## API Reference

### ICamera

```cpp
bool init(const CameraConfig& config);
void deinit();
FrameView capture_frame();  // Returns view of captured frame
void release_frame();       // Must call after capture
bool set_resolution(Resolution res);
bool set_quality(uint8_t quality);
```

### IClock

```cpp
int64_t now_us() const;
int64_t now_ms() const;
void delay_ms(uint32_t ms);
void yield();
```

### StreamingService

```cpp
bool init(const StreamingConfig& config);
bool start();
void stop();
bool get_frame(const uint8_t** data, size_t* size, uint32_t timeout_ms);
void release_frame();
const StreamingStats& stats() const;
```

### FrameBuffer

```cpp
bool init(size_t num_slots, size_t max_frame_size, bool use_psram);
bool push(const uint8_t* data, size_t size, int64_t timestamp_us);
bool peek(const uint8_t** data, size_t* size, int64_t* ts);
void pop();
size_t available() const;
uint32_t frames_dropped() const;
```

## What This Project Teaches

1. **Dependency Injection** in embedded C++ (interfaces, not templates)
2. **Producer-Consumer Pattern** for smooth data flow
3. **Unit Testing Embedded Code** via hardware abstraction
4. **Circular Buffer** with overflow handling
5. **Mock Objects** for deterministic testing
6. **Catch2 Framework** with CMake integration
7. **Dual-Build System** (ESP-IDF for target, CMake for host tests)
