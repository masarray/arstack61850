param(
    [string]$BuildDir = "build-mms-direct-control"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Source = Join-Path $Root "tools\mms_direct_control_server"
$Build = Join-Path $Root $BuildDir

cmake -S $Source -B $Build -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build $Build --config Release --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

ctest --test-dir $Build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$candidates = @(
    (Join-Path $Build "Release\ariec61850_mms_direct_control_server.exe"),
    (Join-Path $Build "ariec61850_mms_direct_control_server.exe")
)
$exe = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) {
    throw "Build succeeded but ariec61850_mms_direct_control_server.exe was not found."
}

Write-Host "[OK] MMS Direct Control server: $exe"
