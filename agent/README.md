# C++ Metrics Agent

The C++ component responsible for collecting system metrics and sending them to the backend.

## Features

- Collects total CPU usage (overall)
- Collects top 5 processes by CPU usage
- Sends metrics as JSON via HTTP POST every 2 seconds (configurable)
- Graceful shutdown on SIGINT/SIGTERM
- Cross-platform (Windows, Linux, macOS)

## Building

### Windows
```bash
# Prerequisites: CMake 3.10+, Visual Studio Build Tools, libcurl
cd agent
build.bat
```

### Linux/macOS
```bash
# Prerequisites: CMake 3.10+, libcurl development files, GCC/Clang
cd agent
./build.sh
```

## Running

```bash
./metrics_agent --backend-url http://localhost:8000 --interval 2
```

### Arguments
- `--backend-url`: URL of the backend service (default: http://localhost:8000)
- `--interval`: Collection interval in seconds (default: 2)

## Docker Build

```bash
docker build -t metrics-agent:latest -f agent/Dockerfile .
```

## Architecture

- **metrics_collector.h/.cpp**: Collects system metrics using platform-specific APIs
  - Windows: Uses Windows API and PDH (Performance Data Helper)
  - Linux/macOS: Uses /proc filesystem or system calls
  
- **http_client.h/.cpp**: Sends metrics to backend via HTTP
  - Uses libcurl for HTTP requests
  - Converts metrics to JSON format
  
- **main.cpp**: Entry point and main loop
  - Manages collection intervals
  - Handles graceful shutdown

## Metrics Format

The agent sends metrics in the following JSON format:

```json
{
  "timestamp": 1707662400,
  "total_cpu_percent": 45.2,
  "top_processes": [
    {
      "pid": 1234,
      "name": "chrome.exe",
      "cpu_percent": 15.5,
      "memory_mb": 512.3
    }
  ]
}
```

## Platform-Specific Notes

### Windows
- Uses Windows API for process enumeration
- PDH (Performance Data Helper) for CPU usage calculation
- Requires Windows Build Tools and libcurl

### Linux/macOS
- Implementation needs to be completed using /proc filesystem (Linux) or system statistics APIs (macOS)
- Requires development headers for system libraries
