# ESP32-S3 WiFi Camera - Build Automation
#
# Usage:
#   make setup       - First-time setup (set target to esp32s3)
#   make build       - Build the project
#   make flash       - Flash to device
#   make monitor     - Open serial monitor
#   make run         - Flash and monitor (most common)
#   make menuconfig  - Configure project settings
#   make test        - Run host-based unit tests
#   make coverage    - Run tests with coverage report
#   make clean       - Clean build artifacts
#   make fullclean   - Full clean (removes sdkconfig too)

# Serial port - override with: make flash PORT=/dev/ttyUSB0
PORT ?= /dev/tty.usbmodem5AB90115901

# Build directories
BUILD_DIR := build
TEST_BUILD_DIR := build-host-tests
COVERAGE_BUILD_DIR := build-coverage

# Default target
.PHONY: all
all: build

# Help
.PHONY: help
help:
	@echo "ESP32-S3 WiFi Camera - Available targets:"
	@echo ""
	@echo "  Setup & Configuration:"
	@echo "    make setup       - First-time setup (set target to esp32s3)"
	@echo "    make menuconfig  - Open configuration menu"
	@echo ""
	@echo "  Build & Flash:"
	@echo "    make build       - Build the project"
	@echo "    make flash       - Flash to device"
	@echo "    make monitor     - Open serial monitor"
	@echo "    make run         - Flash and monitor (most common)"
	@echo ""
	@echo "  Testing:"
	@echo "    make test        - Run host-based unit tests"
	@echo "    make test-verbose - Run tests with verbose output"
	@echo "    make coverage    - Run tests with coverage report"
	@echo ""
	@echo "  Cleanup:"
	@echo "    make clean       - Clean build artifacts"
	@echo "    make fullclean   - Full clean (removes sdkconfig too)"
	@echo "    make test-clean  - Clean test build directories"
	@echo ""
	@echo "  Options:"
	@echo "    PORT=/dev/xxx    - Override serial port (current: $(PORT))"
	@echo ""
	@echo "  Examples:"
	@echo "    make run"
	@echo "    make flash PORT=/dev/ttyUSB0"
	@echo "    make test"

# ==============================================================================
# ESP-IDF Targets
# ==============================================================================

.PHONY: setup
setup:
	idf.py set-target esp32s3

.PHONY: build
build:
	idf.py build

.PHONY: flash
flash:
	idf.py -p $(PORT) flash

.PHONY: monitor
monitor:
	idf.py -p $(PORT) monitor

.PHONY: run
run:
	idf.py -p $(PORT) flash monitor

.PHONY: menuconfig
menuconfig:
	idf.py menuconfig

.PHONY: clean
clean:
	idf.py fullclean

.PHONY: fullclean
fullclean:
	idf.py fullclean
	rm -f sdkconfig sdkconfig.old

# ==============================================================================
# Host Test Targets
# ==============================================================================

$(TEST_BUILD_DIR):
	mkdir -p $(TEST_BUILD_DIR)

.PHONY: test-build
test-build: $(TEST_BUILD_DIR)
	cd $(TEST_BUILD_DIR) && cmake -DBUILD_TESTS=ON .. && cmake --build . -j

.PHONY: test
test: test-build
	cd $(TEST_BUILD_DIR) && ctest --output-on-failure

.PHONY: test-verbose
test-verbose: test-build
	cd $(TEST_BUILD_DIR) && ctest --output-on-failure --verbose

# Run specific test by tag, e.g.: make test-tag TAG="[FrameBuffer]"
.PHONY: test-tag
test-tag: test-build
	cd $(TEST_BUILD_DIR) && ./wifi_camera_tests "$(TAG)"

.PHONY: test-clean
test-clean:
	rm -rf $(TEST_BUILD_DIR) $(COVERAGE_BUILD_DIR)

# ==============================================================================
# Coverage Targets
# ==============================================================================

$(COVERAGE_BUILD_DIR):
	mkdir -p $(COVERAGE_BUILD_DIR)

.PHONY: coverage
coverage: $(COVERAGE_BUILD_DIR)
	cd $(COVERAGE_BUILD_DIR) && cmake -DBUILD_TESTS=ON -DCOVERAGE=ON .. && cmake --build . -j
	cd $(COVERAGE_BUILD_DIR) && cmake --build . --target coverage
	@echo ""
	@echo "Coverage report: $(COVERAGE_BUILD_DIR)/coverage_report/index.html"

.PHONY: coverage-open
coverage-open: coverage
	open $(COVERAGE_BUILD_DIR)/coverage_report/index.html

# ==============================================================================
# Convenience Aliases
# ==============================================================================

.PHONY: b f m r mc t
b: build
f: flash
m: monitor
r: run
mc: menuconfig
t: test
