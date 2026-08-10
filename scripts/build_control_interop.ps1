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

$InventoryCandidateA = Join-Path $Build "$Configuration\ariec61850_control_inventory_probe.exe"
$InventoryCandidateB = Join-Path $Build 'ariec61850_control_inventory_probe.exe'
$InventoryExe = if (Test-Path $InventoryCandidateA) { $InventoryCandidateA } else { $InventoryCandidateB }

if (-not (Test-Path $Exe)) {
    throw "Build succeeded but control interop executable was not found."
}
if (-not (Test-Path $InventoryExe)) {
    throw "Build succeeded but read-only control inventory executable was not found."
}

Write-Host ""
Write-Host "[C5] READY: $Exe"
Write-Host "[C5] INVENTORY READY: $InventoryExe"
Write-Host "[C5] Safest first command for an unknown IED model (read-only, zero control Write):"
Write-Host "  `"$InventoryExe`" <IED-IP> 102 --evidence c5-control-inventory.json"
Write-Host ""
Write-Host "[C5] After inventory identifies an exact control object, descriptor discovery remains read-only:"
Write-Host "  `"$Exe`" <IED-IP> 102 --object <LD/LN.DO> --evidence c5-discovery.json"
Write-Host ""
Write-Host "Live Write remains locked unless the interop executable receives:"
Write-Host "  --arm IEC61850-LAB-CONTROL"
