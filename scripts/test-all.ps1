<#
.SYNOPSIS
Run all project tests (C++ and Python).

.DESCRIPTION
Builds and runs C++ unit tests in `agent/` using CMake + CTest, then runs
Python backend tests in `backend/` using pytest. C++ tests are executed in a
reusable Docker test image to avoid local toolchain dependency issues.

.USAGE
powershell -ExecutionPolicy Bypass -File .\scripts\test-all.ps1
#>

$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$agentSourceDir = Join-Path $repoRoot 'agent'
$agentBuildDir = Join-Path $agentSourceDir 'build'
$backendDir = Join-Path $repoRoot 'backend'
$backendVenvPython = Join-Path $backendDir '.venv\Scripts\python.exe'
$agentSourceDirDocker = ($agentSourceDir -replace '\\', '/')
$cppTestImage = 'metrics-agent-test-runner:latest'
$cppTestDockerfile = Join-Path $repoRoot 'scripts\testing\agent-test-runner.Dockerfile'

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Command,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,

        [Parameter(Mandatory = $true)]
        [string]$ErrorMessage
    )

    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$ErrorMessage (exit code: $LASTEXITCODE)"
    }
}

Push-Location $repoRoot

try {
    Write-Host '=== Running C++ tests (agent) ==='
    Write-Host 'Running C++ tests in Docker test image...'
    Invoke-CheckedCommand -Command 'docker' -Arguments @(
        'build',
        '-t', $cppTestImage,
        '-f', $cppTestDockerfile,
        $repoRoot
    ) -ErrorMessage 'Building C++ Docker test image failed'

    Invoke-CheckedCommand -Command 'docker' -Arguments @(
        'run',
        '--rm',
        '-v', "${agentSourceDirDocker}:/app",
        '-w', '/app',
        $cppTestImage,
        'bash',
        '-lc',
        'cmake -S . -B build-docker-tests -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release && cmake --build build-docker-tests -j && ctest --test-dir build-docker-tests --output-on-failure'
    ) -ErrorMessage 'C++ Docker tests failed'

    Write-Host ''
    Write-Host '=== Running Python tests (backend) ==='

    if (-not (Test-Path $backendVenvPython)) {
        Write-Host 'Creating backend virtual environment...'
        Invoke-CheckedCommand -Command 'py' -Arguments @('-m', 'venv', (Join-Path $backendDir '.venv')) -ErrorMessage 'Creating backend virtual environment failed'
    }

    Invoke-CheckedCommand -Command $backendVenvPython -Arguments @('-m', 'pip', 'install', '-r', (Join-Path $backendDir 'requirements.txt')) -ErrorMessage 'Installing backend test dependencies failed'
    Invoke-CheckedCommand -Command $backendVenvPython -Arguments @('-m', 'pytest', (Join-Path $backendDir 'tests'), '-q') -ErrorMessage 'Backend Python tests failed'

    Write-Host ''
    Write-Host 'All tests passed.' -ForegroundColor Green
}
finally {
    Pop-Location
}
