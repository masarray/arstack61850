param(
    [string]$BuildDir = "build-dynamic-rcb-trial",
    [ValidateSet("Release", "Debug", "RelWithDebInfo")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root "tools\dynamic_rcb_trial"
$build = Join-Path $root $BuildDir

cmake -S $source -B $build -DCMAKE_BUILD_TYPE=$Configuration
cmake --build $build --config $Configuration --parallel
ctest --test-dir $build -C $Configuration --output-on-failure

$candidates = @(
    (Join-Path $build "$Configuration\ariec61850_dynamic_rcb_trial.exe"),
    (Join-Path $build "ariec61850_dynamic_rcb_trial.exe")
)
$exe = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) {
    throw "ariec61850_dynamic_rcb_trial.exe was not found after build."
}

Write-Host ""
Write-Host "Dynamic RCB trial executable:"
Write-Host "  $exe"
Write-Host ""
Write-Host "Read-only plan example:"
Write-Host "  `"$exe`" 192.168.1.10 --no-urcb-fallback --auto-members 4"
Write-Host ""
Write-Host "Armed lab lifecycle example:"
Write-Host "  `"$exe`" 192.168.1.10 --no-urcb-fallback --auto-members 4 --arm IEC61850-LAB-DYNAMIC-RCB"

$staticCandidates = @(
    (Join-Path $build "$Configuration\ariec61850_static_rcb_trial.exe"),
    (Join-Path $build "ariec61850_static_rcb_trial.exe")
)
$staticExe = $staticCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $staticExe) {
    throw "ariec61850_static_rcb_trial.exe was not found after build."
}

Write-Host ""
Write-Host "Static RCB trial executable:"
Write-Host "  $staticExe"
Write-Host ""
Write-Host "Read-only static plan example:"
Write-Host "  `"$staticExe`" 192.168.1.10 --no-urcb-fallback"
Write-Host ""
Write-Host "Armed static subscription example:"
Write-Host "  `"$staticExe`" 192.168.1.10 --no-urcb-fallback --arm IEC61850-LAB-STATIC-RCB"
