cmake_minimum_required(VERSION 3.10)

# Project metadata
# - MetricsAgent: native C++ service that collects system metrics and reports them over HTTP.
# - Minimum CMake version 3.10 to support modern target-based commands.
project(MetricsAgent)

# Language standard configuration
# - Enforce C++17 features to support chrono utilities, filesystem, and modern STL.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Third-party dependencies
# - libcurl is used for HTTP(S) requests to the metrics backend.
#   Ensure libcurl is installed and discoverable by CMake.
find_package(CURL REQUIRED)

# Include paths for project headers
# - The "include" folder contains public headers used across source files.
include_directories(include)

# Source files for the agent executable
# - Add new .cpp files here when expanding functionality.
set(SOURCES
    src/main.cpp
    src/metrics_collector.cpp
    src/http_client.cpp
    src/agent_config.cpp
    src/structured_logger.cpp
)

# Build the main executable target
add_executable(metrics_agent ${SOURCES})

# Link external libraries
# - CURL::libcurl is the imported target provided by find_package(CURL).
target_link_libraries(metrics_agent PRIVATE CURL::libcurl)

# Platform-specific libraries
# - Windows requires PDH for performance counters, PSAPI for process info,
#   and WER for Windows Error Reporting APIs used by some system calls.
if(WIN32)
    target_link_libraries(metrics_agent PRIVATE pdh psapi wer)
endif()

# Unit tests
# - Uses Catch2 (header-only test framework) via FetchContent.
include(CTest)

if(BUILD_TESTING)
    include(FetchContent)

    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.4
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(http_client_tests
        tests/http_client_test.cpp
        src/http_client.cpp
    )

    add_executable(metrics_collector_tests
        tests/metrics_collector_test.cpp
        src/metrics_collector.cpp
    )

    target_include_directories(http_client_tests PRIVATE include)
    target_include_directories(metrics_collector_tests PRIVATE include)
    target_link_libraries(http_client_tests PRIVATE Catch2::Catch2WithMain CURL::libcurl)
    target_link_libraries(metrics_collector_tests PRIVATE Catch2::Catch2WithMain)

    if(WIN32)
        target_link_libraries(http_client_tests PRIVATE pdh psapi wer)
        target_link_libraries(metrics_collector_tests PRIVATE pdh psapi wer)
    endif()

    include(Catch)
    catch_discover_tests(http_client_tests)
    catch_discover_tests(metrics_collector_tests)
endif()
