<#
.SYNOPSIS
Build all Docker images without cache.

.DESCRIPTION
Builds backend, agent, and dashboard images from the repository root
using --no-cache to force fresh layer rebuilds.

.USAGE
powershell -ExecutionPolicy Bypass -File .\scripts\build-images.ps1

.NOTES
- Use this script for clean rebuilds, troubleshooting, or release verification.
- Tags produced: metrics-backend:latest, metrics-agent:latest, metrics-dashboard:latest.
#>

$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
Push-Location $repoRoot

try {
    Write-Host "Building metrics-backend:latest (no cache)..."
    docker build --no-cache -t metrics-backend:latest -f backend/Dockerfile .

    Write-Host "Building metrics-agent:latest (no cache)..."
    docker build --no-cache -t metrics-agent:latest -f agent/Dockerfile agent

    Write-Host "Building metrics-dashboard:latest (no cache)..."
    docker build --no-cache -t metrics-dashboard:latest -f dashboard/Dockerfile .

    Write-Host "All images built successfully."
}
finally {
    Pop-Location
}
