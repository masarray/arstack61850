param(
    [Parameter(Mandatory = $true)]
    [string]$Pcap,

    [string]$BuildDirectory = "build-lab"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Pcap -PathType Leaf)) {
    throw "PCAP file not found: $Pcap"
}

cmake -S . -B $BuildDirectory -A x64 `
    -DARIEC61850_BUILD_TESTS=ON `
    -DARIEC61850_BUILD_TOOLS=ON `
    -DARIEC61850_WARNINGS_AS_ERRORS=ON
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build $BuildDirectory --config Release --parallel `
    --target ariec61850_pcap_interop_check
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$tool = Join-Path $BuildDirectory "Release/ariec61850_pcap_interop_check.exe"
$report = [System.IO.Path]::ChangeExtension(
    (Resolve-Path -LiteralPath $Pcap).Path,
    ".interop.json")

Write-Host "Capture SHA-256:"
Get-FileHash -LiteralPath $Pcap -Algorithm SHA256 | Format-List

& $tool $Pcap --json | Tee-Object -FilePath $report
$exitCode = $LASTEXITCODE
Write-Host "Evidence report: $report"
exit $exitCode
