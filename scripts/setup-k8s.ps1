<#
.SYNOPSIS
Recreate Kubernetes resources and print dashboard port.

.DESCRIPTION
Runs Kubernetes manifest refresh with delete/apply, then fetches the
`dashboard` service port and prints it to the console.

.USAGE
powershell -ExecutionPolicy Bypass -File .\scripts\setup-k8s.ps1
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
    Write-Host "Deleting previous Kubernetes resources from metrics-app.yaml..."
    Invoke-CheckedCommand -Command 'kubectl' -Arguments @('delete', '-f', 'metrics-app.yaml', '--ignore-not-found') -ErrorMessage 'Kubernetes delete failed'

    Write-Host "Applying Kubernetes resources from metrics-app.yaml..."
    Invoke-CheckedCommand -Command 'kubectl' -Arguments @('apply', '-f', 'metrics-app.yaml') -ErrorMessage 'Kubernetes apply failed'

    $dashboardNodePort = & kubectl get svc dashboard -o jsonpath='{.spec.ports[0].nodePort}'
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to query dashboard service NodePort (exit code: $LASTEXITCODE)"
    }

    $dashboardPort = if ([string]::IsNullOrWhiteSpace($dashboardNodePort)) {
        $servicePort = & kubectl get svc dashboard -o jsonpath='{.spec.ports[0].port}'
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to query dashboard service port (exit code: $LASTEXITCODE)"
        }
        $servicePort
    } else {
        $dashboardNodePort
    }

    Write-Host "Dashboard service port: " -NoNewline
    Write-Host "$dashboardPort" -ForegroundColor Green
    Write-Host "Current dashboard service details:"
    Invoke-CheckedCommand -Command 'kubectl' -Arguments @('get', 'svc', 'dashboard') -ErrorMessage 'Failed to print dashboard service details'
}
finally {
    Pop-Location
}
