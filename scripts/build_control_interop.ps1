param(
    [ValidateSet('Release','Debug')]
    [string]$Configuration = 'Release',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root 'build-control-interop'

if ($Clean -and (Test-Path $Build)) {
    Remove-Item -Recurse -Force $Build
}

Write-Host "[C5] Configuring IEC 61850 Control Interop harness..."
cmake -S (Join-Path $Root 'tools/control_interop_probe') -B $Build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[C5] Building $Configuration..."
cmake --build $Build --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[C5] Running offline safety tests..."
ctest --test-dir $Build -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$CandidateA = Join-Path $Build "$Configuration\ariec61850_control_interop_probe.exe"
$CandidateB = Join-Path $Build 'ariec61850_control_interop_probe.exe'
$Exe = if (Test-Path $CandidateA) { $CandidateA } else { $CandidateB }

if (-not (Test-Path $Exe)) {
    throw "Build succeeded but control interop executable was not found."
}

Write-Host ""
Write-Host "[C5] READY: $Exe"
Write-Host "[C5] Safe first command (read-only):"
Write-Host "  `"$Exe`" <IED-IP> 102 --object LD0/CSWI1.Pos --evidence c5-discovery.json"
Write-Host ""
Write-Host "Live Write remains locked unless the executable receives:"
Write-Host "  --arm IEC61850-LAB-CONTROL"
