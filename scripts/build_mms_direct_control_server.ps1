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

function Find-BuiltExe([string]$Name) {
    $candidates = @(
        (Join-Path $Build "Release\$Name.exe"),
        (Join-Path $Build "$Name.exe")
    )
    return $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}

$controlExe = Find-BuiltExe "ariec61850_mms_direct_control_server"
$urcbExe = Find-BuiltExe "ariec61850_mms_urcb_server"
if (-not $controlExe) {
    throw "Build succeeded but ariec61850_mms_direct_control_server.exe was not found."
}
if (-not $urcbExe) {
    throw "Build succeeded but ariec61850_mms_urcb_server.exe was not found."
}

Write-Host "[OK] MMS Direct Control server: $controlExe"
Write-Host "[OK] MMS URCB Reporting server: $urcbExe"
