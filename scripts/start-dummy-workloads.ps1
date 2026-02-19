<#
.SYNOPSIS
Start dummy workload containers to generate visible CPU/memory/I/O/thread activity.

.DESCRIPTION
Launches six long-running Python containers on a Docker network (default: metrics-net).
Workloads are intentionally synthetic to help populate dashboard top-process rows.

.USAGE
powershell -ExecutionPolicy Bypass -File .\scripts\start-dummy-workloads.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\start-dummy-workloads.ps1 -NetworkName metrics-net
#>

param(
    [string]$NetworkName = "metrics-net"
)

$ErrorActionPreference = 'Stop'

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

$networkExists = & docker network ls --format "{{.Name}}" | Where-Object { $_ -eq $NetworkName }
if (-not $networkExists) {
    throw "Docker network '$NetworkName' was not found. Create it first (e.g. docker network create $NetworkName)."
}

$image = "python:3.11-alpine"

$workloads = @(
    @{ Name = "dummy-cpu"; Code = "import math,time;`nwhile True:`n  [math.sqrt((i+1)*(i+1)) for i in range(250000)]`n  time.sleep(0.02)" },
    @{ Name = "dummy-memory"; Code = "import os,time;`nchunks=[]`nwhile True:`n  chunks.append('x'*4_000_000)`n  if len(chunks)>80:`n    chunks=chunks[-40:]`n  time.sleep(0.05)" },
    @{ Name = "dummy-io"; Code = "import os,time;`np='/tmp/dummy-io.bin'`nwhile True:`n  with open(p,'wb') as f: f.write(os.urandom(4*1024*1024))`n  with open(p,'rb') as f: f.read()`n  time.sleep(0.05)" },
    @{ Name = "dummy-threads"; Code = "import threading,time,hashlib,os;`ndef w():`n  while True:`n    hashlib.sha256(os.urandom(2048)).hexdigest()`nfor _ in range(24):`n  threading.Thread(target=w,daemon=True).start()`nwhile True: time.sleep(1)" },
    @{ Name = "dummy-mixed"; Code = "import threading,time,os,hashlib;`ndef cpu():`n  while True: hashlib.md5(os.urandom(4096)).hexdigest()`ndef io():`n  while True:`n    with open('/tmp/mixed.bin','ab') as f: f.write(os.urandom(128*1024))`n    time.sleep(0.01)`nfor _ in range(8): threading.Thread(target=cpu,daemon=True).start()`nfor _ in range(4): threading.Thread(target=io,daemon=True).start()`nwhile True: time.sleep(1)" },
    @{ Name = "dummy-fd"; Code = "import os,time;`nfiles=[]`nwhile True:`n  files.append(open('/dev/null','rb'))`n  if len(files)>400:`n    [f.close() for f in files[:200]]`n    files=files[200:]`n  time.sleep(0.01)" }
)

foreach ($w in $workloads) {
    $containerName = "metrics-$($w.Name)"

    $existing = & docker ps -a --filter "name=^/$containerName$" --format "{{.Names}}"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to query existing container '$containerName' (exit code: $LASTEXITCODE)"
    }

    if ($existing -eq $containerName) {
        Invoke-CheckedCommand `
            -Command 'docker' `
            -Arguments @('rm', '-f', $containerName) `
            -ErrorMessage "Failed to remove existing container $containerName"
    }

    Invoke-CheckedCommand `
        -Command 'docker' `
        -Arguments @(
            'run', '-d',
            '--name', $containerName,
            '--network', $NetworkName,
            '--label', 'metrics.workload=dummy',
            '--restart', 'unless-stopped',
            $image,
            'python', '-u', '-c', $w.Code
        ) `
        -ErrorMessage "Failed to start $containerName"

    Write-Host "Started $containerName" -ForegroundColor Green
}

Write-Host "Dummy workloads are running on network '$NetworkName'." -ForegroundColor Green
Write-Host "List them: docker ps --filter label=metrics.workload=dummy"
Write-Host "Stop/remove them: docker rm -f `(docker ps -aq --filter label=metrics.workload=dummy`)"
