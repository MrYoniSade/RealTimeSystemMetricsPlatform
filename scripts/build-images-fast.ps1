<#
.SYNOPSIS
Build all Docker images using cache.

.DESCRIPTION
Builds backend, agent, and dashboard images from the repository root
with normal Docker layer caching for faster local rebuilds.

.USAGE
powershell -ExecutionPolicy Bypass -File .\scripts\build-images-fast.ps1

.NOTES
- Use this script for everyday development loops.
- Tags produced: metrics-backend:latest, metrics-agent:latest, metrics-dashboard:latest.
#>

$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
Push-Location $repoRoot

try {
    Write-Host "Building metrics-backend:latest (with cache)..."
    docker build -t metrics-backend:latest -f backend/Dockerfile .

    Write-Host "Building metrics-agent:latest (with cache)..."
    docker build -t metrics-agent:latest -f agent/Dockerfile agent

    Write-Host "Building metrics-dashboard:latest (with cache)..."
    docker build -t metrics-dashboard:latest -f dashboard/Dockerfile .

    Write-Host "All images built successfully."
}
finally {
    Pop-Location
}
