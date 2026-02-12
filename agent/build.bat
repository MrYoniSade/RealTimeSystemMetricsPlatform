@echo off
REM Build script for the C++ agent on Windows

setlocal enabledelayedexpansion

echo Building C++ Metrics Agent...

cd /d "%~dp0"

REM Create build directory
if not exist build mkdir build
cd build

REM Run CMake and build
cmake ..
cmake --build . --config Release

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Build complete! Executable: .\Release\metrics_agent.exe
    echo Usage: metrics_agent.exe --backend-url http://localhost:8000 --interval 2
) else (
    echo Build failed!
    exit /b 1
)
