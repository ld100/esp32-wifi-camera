# ESP32-S3 WiFi Camera

MJPEG video streaming camera built on the ESP32-S3 microcontroller. Captures frames from an OV2640 camera module, buffers them using a producer-consumer ring buffer, and streams live video over HTTP to any web browser on your local network.

The project uses dependency injection through abstract hardware interfaces (`ICamera`, `IClock`), which allows the core logic to be unit tested on your development machine without any ESP32 hardware.

## Hardware Requirements

- **ESP32-S3** development board with PSRAM (e.g., ESP32-S3-EYE, Freenove ESP32-S3-WROOM)
- **OV2640** camera module (typically included on camera-oriented dev boards)
- USB cable for flashing and serial monitor

## Prerequisites

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/) v5.x (with `idf.py` available in your PATH)
- CMake 3.16+
- For host tests: a C++17 compiler (GCC or Clang), `lcov` for coverage reports

### macOS

```bash
brew install cmake lcov
```

### Linux (Ubuntu/Debian)

```bash
sudo apt-get install cmake build-essential lcov
```

## Quick Start

```bash
# 1. Clone the repository
git clone https://github.com/ld100/esp32-wifi-camera.git
cd esp32-wifi-camera

# 2. First-time setup (sets chip target to ESP32-S3)
make setup

# 3. Configure WiFi credentials and other settings
make menuconfig

# 4. Build, flash, and open serial monitor
make run
```

Once running, open `http://espcam.local` (or the IP address shown in the serial monitor) in a browser to view the live stream.

## Build Commands

The project includes a Makefile that wraps common `idf.py` and CMake commands.

Run `make help` for the full list. Here are the most useful targets:

| Command | Description |
|---------|-------------|
| `make setup` | First-time setup (set target to ESP32-S3) |
| `make menuconfig` | Open the configuration menu (WiFi, FPS, etc.) |
| `make build` | Build the firmware |
| `make flash` | Flash firmware to the device |
| `make monitor` | Open the serial monitor |
| `make run` | Flash and monitor in one step |
| `make test` | Build and run host-based unit tests |
| `make test-verbose` | Run tests with verbose output |
| `make coverage` | Generate test coverage report |
| `make clean` | Clean build artifacts |
| `make fullclean` | Full clean including `sdkconfig` |

Single-letter aliases are available: `make b` (build), `make f` (flash), `make m` (monitor), `make r` (run), `make t` (test).

Override the serial port with `PORT=`:

```bash
make flash PORT=/dev/ttyUSB0
make run PORT=/dev/tty.usbmodem1234
```

## Configuration

All settings are available through `make menuconfig` under **WiFi Camera Configuration**.

### WiFi Settings

| Setting | Default | Description |
|---------|---------|-------------|
| WiFi SSID | - | Network name to connect to |
| WiFi Password | - | WPA2 password |
| Connection Timeout | 15000 ms | Max wait time for WiFi connection |

### Network Settings

| Setting | Default | Description |
|---------|---------|-------------|
| mDNS Hostname | `espcam` | Access the camera at `espcam.local` |
| HTTP Server Port | 80 | Port for the web server |

### Camera Settings

| Setting | Default | Range | Description |
|---------|---------|-------|-------------|
| JPEG Quality | 12 | 10-63 | Lower = better quality, larger files |
| DMA Frame Buffers | 2 | 1-3 | More buffers = smoother capture, more memory |

### Streaming Settings

| Setting | Default | Range | Description |
|---------|---------|-------|-------------|
| Target FPS | 8 | 1-15 | Frames per second |
| Buffer Slots | 4 | 2-8 | Ring buffer size (PSRAM) |
| Max Frame Size | 100 KB | 50-200 KB | Max size of a single JPEG frame |
| Consumer Timeout | 1000 ms | 100-5000 | How long to wait for a new frame |

## HTTP Endpoints

Once the camera is running and connected to WiFi:

| Endpoint | Description |
|----------|-------------|
| `GET /` | HTML viewer page with embedded stream |
| `GET /stream` | MJPEG multipart stream (for direct use or embedding) |
| `GET /capture` | Single JPEG frame snapshot |
| `GET /status` | JSON with frame counters and system statistics |

## Architecture and Design

### Streaming Pipeline

```
[Camera] --> [Producer Task] --> [FrameBuffer (ring)] --> [HTTP Handler] --> [Browser]
  (ICamera)    (fixed rate)       (N slots, PSRAM)          (MJPEG)
```

The system is built around a **producer-consumer pattern**. A dedicated producer task captures frames from the camera at a fixed interval (e.g., 125ms at 8 FPS) and pushes them into a thread-safe ring buffer. The HTTP handler acts as the consumer -- it blocks until a frame is available, then sends it as part of a multipart MJPEG response. The ring buffer decouples the two sides so that variable camera capture times and network latency don't cause stuttering.

### Design Patterns

#### Dependency Injection

All hardware access goes through abstract C++ interfaces (`ICamera`, `IClock`) defined with pure virtual methods. Production code instantiates concrete drivers (`EspCameraDriver`, `EspClockDriver`) and injects them into the `StreamingService` via constructor references:

```cpp
// Production wiring in main.cpp
drivers::EspCameraDriver camera(pins);
drivers::EspClockDriver clock;
core::StreamingService streaming(camera, clock);  // injected by reference
```

Tests substitute mock implementations (`MockCamera`, `MockClock`) for the same interfaces, so the full streaming pipeline logic runs on a host machine with no hardware:

```cpp
// Test wiring
mocks::MockCamera camera;
mocks::MockClock clock;
core::StreamingService streaming(camera, clock);  // same interface, mock behavior
```

This is interface-based DI (virtual dispatch), chosen over template-based DI for simplicity and because the virtual call overhead is negligible compared to camera capture and network I/O.

#### Circular Buffer with Overflow Policy

`FrameBuffer` is a fixed-size ring buffer with pre-allocated memory slots. When the buffer is full, the oldest frame is silently dropped to make room for the new one -- this "drop oldest" policy keeps the stream showing the most recent data rather than falling behind. A `reading` flag on each slot prevents the producer from overwriting a frame that the consumer is currently sending to a client.

#### Mock Objects

The mock implementations (`MockCamera`, `MockClock`) support:

- **Configurable outcomes** -- success or failure for any operation (`set_capture_result(false)`)
- **Custom frame data** -- inject specific byte sequences to test edge cases
- **Delay simulation** -- register callbacks that execute during capture to simulate real timing
- **Call tracking** -- counters for every method (`capture_calls()`, `release_calls()`) to verify correct interaction sequences

This makes tests deterministic and fast, with no hardware timing dependencies.

### Platform Abstraction (Dual Build)

The project compiles from the same source for two targets using a single `CMakeLists.txt`:

- **ESP32-S3 target** (`IDF_PATH` set): builds with ESP-IDF, uses FreeRTOS primitives (`xSemaphoreCreateMutex`, `xTaskCreatePinnedToCore`, `vTaskDelay`), allocates frame buffers in PSRAM.
- **Host tests** (`-DBUILD_TESTS=ON`): builds with CMake + Catch2, uses C++ standard library (`std::mutex`, `std::thread`, `std::condition_variable`), allocates on the heap.

Compile-time `#ifdef ESP_PLATFORM` guards select the appropriate primitives. Core logic (`FrameBuffer`, `StreamingService`) is identical on both platforms.

### Host-Based Unit Testing

Tests run natively on macOS or Linux using [Catch2](https://github.com/catchorg/Catch2) v3 (auto-fetched via CMake FetchContent). Because all hardware dependencies are injected through interfaces, the tests exercise the real `FrameBuffer` and `StreamingService` code -- including multi-threaded producer-consumer interaction -- without any ESP32 simulator or emulator.

Test coverage includes:
- **FrameBuffer:** initialization, push/peek/pop sequencing, overflow with drop-oldest, concurrent access from multiple threads, edge cases (zero-size frames, uninitialized buffer)
- **StreamingService:** start/stop lifecycle, frame capture and delivery to consumers, statistics tracking, configuration changes, error handling when capture fails

## Project Structure

```
├── CMakeLists.txt              # Dual-build: ESP-IDF or host tests
├── Makefile                    # Build automation shortcuts
├── sdkconfig.defaults          # Default ESP-IDF settings
├── partitions.csv              # Flash partition layout
├── main/
│   ├── main.cpp                # Wires dependencies, starts services
│   ├── Kconfig.projbuild       # menuconfig definitions
│   ├── idf_component.yml       # ESP component dependencies
│   ├── interfaces/
│   │   ├── i_camera.hpp        # Camera interface
│   │   └── i_clock.hpp         # Clock/time interface
│   ├── drivers/
│   │   ├── esp_camera_driver.hpp
│   │   └── esp_clock_driver.hpp
│   └── core/
│       ├── frame_buffer.hpp    # Thread-safe ring buffer
│       ├── streaming_service.hpp  # Producer-consumer orchestration
│       ├── web_server.hpp      # HTTP + MJPEG endpoints
│       └── wifi_manager.hpp    # WiFi connection management
└── test/
    ├── test_frame_buffer.cpp
    ├── test_streaming_service.cpp
    └── mocks/
        ├── mock_camera.hpp
        └── mock_clock.hpp
```

## Testing

Tests run on the host machine (macOS/Linux) using [Catch2](https://github.com/catchorg/Catch2) v3. No ESP32 hardware is needed.

```bash
# Run all tests
make test

# Run tests matching a Catch2 tag
make test-tag TAG="[FrameBuffer]"

# Run a specific test by name
cd build-host-tests && ./wifi_camera_tests "peek returns oldest frame"
```

### Coverage

```bash
make coverage
# Report is at build-coverage/coverage_report/index.html

# macOS: open in browser
make coverage-open
```

## Memory Usage

| Component | Location | Size |
|-----------|----------|------|
| Frame ring buffer (4 x 100 KB) | PSRAM | ~400 KB |
| Camera DMA buffers | PSRAM | ~150 KB |
| WiFi stack | DRAM | ~40 KB |
| HTTP server | DRAM | ~8 KB |

The ESP32-S3 has 8 MB of PSRAM, so total usage is well within limits.

## License

This project is licensed under the [MIT License](LICENSE).
