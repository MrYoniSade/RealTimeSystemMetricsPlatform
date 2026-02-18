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
    Write-Host "Building metrics-backend:latest (with cache)..."
    Invoke-CheckedCommand -Command 'docker' -Arguments @('build', '-t', 'metrics-backend:latest', '-f', 'backend/Dockerfile', '.') -ErrorMessage 'Backend image build failed'

    Write-Host "Building metrics-agent:latest (with cache)..."
    Invoke-CheckedCommand -Command 'docker' -Arguments @('build', '-t', 'metrics-agent:latest', '-f', 'agent/Dockerfile', 'agent') -ErrorMessage 'Agent image build failed'

    Write-Host "Building metrics-dashboard:latest (with cache)..."
    Invoke-CheckedCommand -Command 'docker' -Arguments @('build', '-t', 'metrics-dashboard:latest', '-f', 'dashboard/Dockerfile', '.') -ErrorMessage 'Dashboard image build failed'

    Write-Host "All images built successfully."
}
finally {
    Pop-Location
}
