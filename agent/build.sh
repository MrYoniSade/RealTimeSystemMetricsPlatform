#!/bin/bash
# Build script for the C++ agent on Linux/macOS

set -e

echo "Building C++ Metrics Agent..."

cd "$(dirname "$0")"

# Create build directory
mkdir -p build
cd build

# Run CMake and build
cmake ..
cmake --build . --config Release

echo "Build complete! Executable: ./metrics_agent"
echo "Usage: ./metrics_agent --backend-url http://localhost:8000 --interval 2"
