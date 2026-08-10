param(
    [Parameter(Mandatory = $true)][string]$HostName,
    [int]$Port = 102,
    [string]$OutputDirectory = ".\phase4c-evidence",
    [int]$Cycles = 10,
    [int]$ContentionCycles = 3,
    [int]$IntervalMs = 1000,
    [int]$TimeoutMs = 30000,
    [string]$IedName = "",
    [string]$ExpectedCSharpModel = "",
    [string]$ContentionRcb = "",
    [int]$ContentionProbeCount = 3,
    [int]$ContentionProbeDelayMs = 1000,
    [int]$ContentionCooldownSec = 60,
    [switch]$FastReadOnly,
    [switch]$AllowWarnings,
    [switch]$AllowContended,
    [switch]$ParityTypes,
    [switch]$ParityRuntime
)

$ErrorActionPreference = "Stop"

$arguments = @(
    "$PSScriptRoot\run-phase4c-physical-acceptance.py",
    $HostName,
    $Port,
    "--output", $OutputDirectory,
    "--cycles", $Cycles,
    "--contention-cycles", $ContentionCycles,
    "--interval-ms", $IntervalMs,
    "--timeout-ms", $TimeoutMs,
    "--contention-probe-count", $ContentionProbeCount,
    "--contention-probe-delay-ms", $ContentionProbeDelayMs,
    "--contention-cooldown-sec", $ContentionCooldownSec
)

if ($IedName) { $arguments += @("--ied-name", $IedName) }
if ($ExpectedCSharpModel) {
    $arguments += @("--expected-csharp-model", $ExpectedCSharpModel)
}
if ($ContentionRcb) { $arguments += @("--contention-rcb", $ContentionRcb) }
if ($FastReadOnly) { $arguments += "--fast-readonly" }
if ($AllowWarnings) { $arguments += "--allow-warnings" }
if ($AllowContended) { $arguments += "--allow-contended" }
if ($ParityTypes) { $arguments += "--parity-types" }
if ($ParityRuntime) { $arguments += "--parity-runtime" }

python @arguments
exit $LASTEXITCODE
