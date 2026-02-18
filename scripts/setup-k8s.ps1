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
Push-Location $repoRoot

try {
    Write-Host "Deleting previous Kubernetes resources from metrics-app.yaml..."
    kubectl delete -f metrics-app.yaml --ignore-not-found

    Write-Host "Applying Kubernetes resources from metrics-app.yaml..."
    kubectl apply -f metrics-app.yaml

    $dashboardNodePort = kubectl get svc dashboard -o jsonpath='{.spec.ports[0].nodePort}'
    $dashboardPort = if ([string]::IsNullOrWhiteSpace($dashboardNodePort)) {
        kubectl get svc dashboard -o jsonpath='{.spec.ports[0].port}'
    } else {
        $dashboardNodePort
    }

    Write-Host "Dashboard service port: " -NoNewline
    Write-Host "$dashboardPort" -ForegroundColor Green
    Write-Host "Current dashboard service details:"
    kubectl get svc dashboard
}
finally {
    Pop-Location
}
