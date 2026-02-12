FROM mcr.microsoft.com/windows/servercore:ltsc2022

# Install vcpkg and dependencies
RUN powershell -Command \
    Invoke-WebRequest -Uri https://github.com/Microsoft/vcpkg/archive/master.zip -OutFile vcpkg.zip ; \
    Expand-Archive -Path vcpkg.zip -DestinationPath C:\ ; \
    Remove-Item vcpkg.zip

# Set up build tools
RUN powershell -Command \
    Invoke-WebRequest -Uri https://cmake.org/files/v3.27/cmake-3.27.0-windows-x86_64.msi -OutFile cmake.msi ; \
    msiexec /i cmake.msi /quiet ; \
    Remove-Item cmake.msi

WORKDIR /app

# Copy source files
COPY agent/ .

# Build the agent
RUN powershell -Command \
    $env:PATH += ';C:\Program Files\CMake\bin' ; \
    mkdir build -ErrorAction SilentlyContinue ; \
    cd build ; \
    cmake .. ; \
    cmake --build . --config Release

ENTRYPOINT ["C:\\app\\build\\Release\\metrics_agent.exe", "--backend-url", "http://backend:8000"]
