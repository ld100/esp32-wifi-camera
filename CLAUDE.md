# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-S3 WiFi camera streaming system with dependency injection for testability. Captures video, buffers frames using producer-consumer pattern, and streams MJPEG over HTTP. The key architectural innovation is hardware abstraction through interfaces (ICamera, IClock) enabling host-based unit testing.

## Build Commands

### ESP32-S3 Target

```bash
idf.py set-target esp32s3
idf.py menuconfig              # Set WiFi credentials under "Project Configuration"
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Host Tests (macOS/Linux)

```bash
mkdir build-host-tests && cd build-host-tests
cmake -DBUILD_TESTS=ON ..
cmake --build . -j
ctest --output-on-failure

# Run single test (Catch2 tag filtering)
./wifi_camera_tests "[FrameBuffer]"
./wifi_camera_tests "peek returns oldest frame"
```

### Coverage Report

```bash
mkdir build-coverage && cd build-coverage
cmake -DBUILD_TESTS=ON -DCOVERAGE=ON ..
cmake --build .
cmake --build . --target coverage
open coverage_report/index.html
```

## Architecture

```
[Camera] → [Producer Task (3 FPS)] → [FrameBuffer (ring)] → [HTTP Handler] → [Browser]
  (ICamera)                           (3 slots, PSRAM)         (MJPEG)
```

**Dependency Injection Pattern:** All hardware accessed through abstract interfaces, enabling mock substitution for testing.

**Dual Build System:** Root CMakeLists.txt routes to:
- `BUILD_TESTS=ON` → Host-based Catch2 tests
- ESP-IDF environment → Target build for ESP32-S3

## Key Components

| Component | Location | Purpose |
|-----------|----------|---------|
| `ICamera`, `IClock` | `main/interfaces/` | Abstract hardware interfaces |
| `EspCameraDriver`, `EspClockDriver` | `main/drivers/` | Production implementations |
| `MockCamera`, `MockClock` | `test/mocks/` | Test doubles |
| `FrameBuffer` | `main/core/frame_buffer.hpp` | Thread-safe ring buffer (PSRAM) |
| `StreamingService` | `main/core/streaming_service.hpp` | Producer-consumer orchestration |
| `WebServer` | `main/core/web_server.hpp` | HTTP + MJPEG endpoints |

## Platform Abstraction

Code uses `#ifdef ESP_PLATFORM` to switch between:
- ESP32: FreeRTOS primitives (`xSemaphoreCreateMutex`, `xTaskCreatePinnedToCore`, `vTaskDelay`)
- Host: C++ standard library (`std::mutex`, `std::thread`, `std::condition_variable`)

## Configuration

Settings in `idf.py menuconfig` under "Project Configuration":
- `CONFIG_STREAM_FPS` (1-15, default 3)
- `CONFIG_WIFI_SSID`, `CONFIG_WIFI_PASSWORD`
- `CONFIG_MDNS_HOSTNAME` (default "espcam" → espcam.local)

## Testing

**Framework:** Catch2 v3.5.2 (auto-fetched via CMake FetchContent)

**Test files:**
- `test/test_frame_buffer.cpp` - FrameBuffer unit tests
- `test/test_streaming_service.cpp` - StreamingService integration tests

**Coverage target:** >80% (excluding ESP-IDF hardware APIs)

## Memory Constraints

- Frame buffers: PSRAM (3×100KB default)
- Camera driver buffers: PSRAM (~150KB)
- Total PSRAM usage: ~450KB (ESP32-S3 has 8MB)
- Use `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` to check PSRAM availability

## HTTP Endpoints (ESP32 only)

- `GET /` → HTML viewer page
- `GET /stream` → MJPEG multipart stream
- `GET /capture` → Single JPEG frame
- `GET /status` → JSON statistics
