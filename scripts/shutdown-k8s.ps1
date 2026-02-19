<#
.SYNOPSIS
Gracefully remove Kubernetes resources for this project.

.USAGE
powershell -ExecutionPolicy Bypass -File .\scripts\shutdown-k8s.ps1
#>

$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
Push-Location $repoRoot

try {
    kubectl delete -f metrics-app.yaml
    if ($LASTEXITCODE -ne 0) {
        throw "Kubernetes delete failed (exit code: $LASTEXITCODE)"
    }
}
finally {
    Pop-Location
}
