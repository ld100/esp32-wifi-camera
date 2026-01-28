# project-04-wifi-camera-v2
# Dual-build: ESP-IDF for target, CMake for host tests

cmake_minimum_required(VERSION 3.16)

# BUILD_TESTS takes priority - check it FIRST
option(BUILD_TESTS "Build unit tests for host" OFF)

if(BUILD_TESTS)
    # =========================================================================
    # Host test build
    # =========================================================================
    project(wifi_camera_tests VERSION 1.0.0 LANGUAGES CXX)
    
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    
    # Fetch Catch2 v3
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.2
    )
    FetchContent_MakeAvailable(Catch2)
    
    # Enable testing
    enable_testing()
    
    # Test executable - Catch2WithMain provides main()
    add_executable(wifi_camera_tests
        test/test_frame_buffer.cpp
        test/test_streaming_service.cpp
    )
    
    target_include_directories(wifi_camera_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/main
        ${CMAKE_CURRENT_SOURCE_DIR}/test
    )
    
    # Link Catch2WithMain (provides main() automatically)
    target_link_libraries(wifi_camera_tests PRIVATE
        Catch2::Catch2WithMain
    )
    
    # Enable threading for tests
    find_package(Threads REQUIRED)
    target_link_libraries(wifi_camera_tests PRIVATE Threads::Threads)
    
    # Compiler warnings
    target_compile_options(wifi_camera_tests PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )
    
    # Register tests with CTest
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(wifi_camera_tests)
    
    # Coverage (optional)
    option(COVERAGE "Enable coverage reporting" OFF)
    if(COVERAGE)
        target_compile_options(wifi_camera_tests PRIVATE --coverage -O0 -g)
        target_link_options(wifi_camera_tests PRIVATE --coverage)
        
        add_custom_target(coverage
            COMMAND ${CMAKE_CURRENT_BINARY_DIR}/wifi_camera_tests
            COMMAND lcov --capture --directory ${CMAKE_CURRENT_BINARY_DIR} --output-file coverage.info --ignore-errors mismatch
            COMMAND lcov --remove coverage.info '/usr/*' '*/test/*' '*/Catch2/*' --output-file coverage.info
            COMMAND genhtml coverage.info --output-directory coverage_report
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Generating coverage report..."
        )
    endif()
    
elseif(DEFINED ENV{IDF_PATH})
    # =========================================================================
    # ESP-IDF build
    # =========================================================================
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    project(wifi_camera)
    
else()
    message(FATAL_ERROR 
        "Neither BUILD_TESTS=ON nor IDF_PATH is set.\n"
        "For ESP32 build: source esp-idf/export.sh\n"
        "For host tests:  cmake -DBUILD_TESTS=ON .."
    )
endif()
