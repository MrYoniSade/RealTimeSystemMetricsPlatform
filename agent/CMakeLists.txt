cmake_minimum_required(VERSION 3.10)
project(MetricsAgent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(CURL REQUIRED)

# Include directories
include_directories(include)

# Source files
set(SOURCES
    src/main.cpp
    src/metrics_collector.cpp
    src/http_client.cpp
)

# Create executable
add_executable(metrics_agent ${SOURCES})

# Link libraries
target_link_libraries(metrics_agent PRIVATE CURL::libcurl)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(metrics_agent PRIVATE pdh psapi wer)
endif()
