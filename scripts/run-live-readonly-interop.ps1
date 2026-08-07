param(
    [Parameter(Mandatory = $true)][string]$HostName,
    [int]$Port = 102,
    [int]$Cycles = 3,
    [int]$IntervalMs = 1000,
    [int]$TimeoutMs = 5000,
    [string]$IedName = "",
    [string]$OutputDirectory = ".\interop-evidence",
    [string]$ExpectedCSharpModel = "",
    [switch]$AllowWarnings
)
$ErrorActionPreference = "Stop"
$arguments = @(
    "$PSScriptRoot\run-live-readonly-interop.py",
    $HostName, $Port,
    "--output", $OutputDirectory,
    "--cycles", $Cycles,
    "--interval-ms", $IntervalMs,
    "--timeout-ms", $TimeoutMs
)
if ($IedName) { $arguments += @("--ied-name", $IedName) }
if ($ExpectedCSharpModel) { $arguments += @("--expected-csharp-model", $ExpectedCSharpModel) }
if ($AllowWarnings) { $arguments += "--allow-warnings" }
python @arguments
exit $LASTEXITCODE
