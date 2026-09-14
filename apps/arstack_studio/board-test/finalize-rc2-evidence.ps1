param(
    [Parameter(Mandatory = $true)]
    [string]$Evidence,

    [string]$Evaluator = ".\arstack_rc2_acceptance.exe",

    [string]$ExpectedSha = "ed46f3bed73d398d71be2eacf34c4995325c3cad",

    [string]$Output = ".\rc2-final-evidence.json"
)

$ErrorActionPreference = 'Stop'

function Read-YesNo([string]$Prompt) {
    while ($true) {
        $answer = (Read-Host "$Prompt [y/n]").Trim().ToLowerInvariant()
        if ($answer -eq 'y' -or $answer -eq 'yes') { return $true }
        if ($answer -eq 'n' -or $answer -eq 'no') { return $false }
        Write-Host 'Please answer y or n.' -ForegroundColor Yellow
    }
}

function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$evidencePath = (Resolve-Path $Evidence).Path
$evaluatorPath = (Resolve-Path $Evaluator).Path
$data = Get-Content -Raw $evidencePath | ConvertFrom-Json

Require ($data.schema -eq 'arstack.studio.rc2.v1') 'Unexpected evidence schema.'
Require ($data.candidateSha -eq $ExpectedSha) "Evidence SHA $($data.candidateSha) does not match frozen candidate $ExpectedSha."
Require ($null -ne $data.boardRunner -and $data.boardRunner.automatedBoardPathPassed -eq $true) 'Automated physical-board path has not passed.'
Require ([int]$data.telemetry.durationSeconds -ge 3600) 'Retained soak is below the 3600-second RC2 minimum.'
Require ([int]$data.telemetry.fpsMin -ge 3999 -and [int]$data.telemetry.fpsMax -le 4001) 'Observed fps window is outside 3999..4001.'
Require ([int64]$data.telemetry.missed -eq 0) 'Missed slots are non-zero.'
Require ([int64]$data.telemetry.txFailures -eq 0) 'Canonical TX failures are non-zero.'
Require ([int]$data.telemetry.healthReconnects -eq 0) 'Health reconnect occurred during retained run.'
Require ([int]$data.telemetry.automaticStarts -eq 0) 'Automatic Start was observed.'

Write-Host ''
Write-Host 'Independent SV capture attestation' -ForegroundColor Cyan
Write-Host 'Use a capture taken from an independent Ethernet observer, not Studio telemetry.'

$captureSeconds = 0
while ($captureSeconds -lt 10) {
    $raw = Read-Host 'Captured SV duration in seconds (minimum 10)'
    [void][int]::TryParse($raw, [ref]$captureSeconds)
    if ($captureSeconds -lt 10) { Write-Host 'At least 10 seconds is required.' -ForegroundColor Yellow }
}

$data.capture.durationSeconds = $captureSeconds
$data.capture.destinationMacMatches = Read-YesNo 'Destination MAC matches the frozen 4I+4V profile'
$data.capture.appidMatches = Read-YesNo 'APPID matches the frozen profile'
$data.capture.vlanPcpMatches = Read-YesNo 'VLAN ID / PCP match the frozen profile'
$data.capture.svIdMatches = Read-YesNo 'svID matches the frozen profile'
$data.capture.confRevMatches = Read-YesNo 'confRev matches the frozen profile'
$data.capture.asduCountMatches = Read-YesNo 'Every checked frame has exactly 1 ASDU'
$data.capture.payloadBytesMatches = Read-YesNo 'Sample payload is 64 bytes'
$data.capture.smpCntContinuous = Read-YesNo 'smpCnt is continuous with no unexplained gaps'
$data.capture.counterWrapObserved = Read-YesNo 'At least one 3999 -> 0 counter wrap is visible'
$liveEditContinuous = Read-YesNo 'Independent capture proves live edits did NOT restart smpCnt'
$data.operations.smpCntRestartOnLiveEdit = -not $liveEditContinuous
$syncZero = Read-YesNo 'smpSynch is advertised as 0 throughout the accepted capture'
$data.capture.smpSynchAdvertised = if ($syncZero) { 0 } else { -1 }

Write-Host ''
Write-Host 'Studio operator observation' -ForegroundColor Cyan
$data.operator.uiResponsive = Read-YesNo 'Studio remained responsive during the retained run'
$noOrphan = Read-YesNo 'After closing Studio, no orphan Studio/worker/firmware process remained'
$data.operator.orphanWorkerObserved = -not $noOrphan

$captureReference = Read-Host 'Capture reference/path/name for the acceptance record'
if ($null -eq $data.boardRunner) { $data | Add-Member -NotePropertyName boardRunner -NotePropertyValue ([pscustomobject]@{}) }
$data.boardRunner | Add-Member -Force -NotePropertyName captureReference -NotePropertyValue $captureReference
$data.boardRunner | Add-Member -Force -NotePropertyName finalizedAtUtc -NotePropertyValue ([DateTime]::UtcNow.ToString('o'))

$data | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 $Output

Write-Host ''
Write-Host "Running fail-closed evaluator against $ExpectedSha ..." -ForegroundColor Cyan
& $evaluatorPath --evidence $Output --expect-sha $ExpectedSha
$code = $LASTEXITCODE
if ($code -ne 0) {
    throw "RC2 evidence evaluator rejected the evidence (exit code $code)."
}

Write-Host "RC2 PHYSICAL EVIDENCE: PASS -> $Output" -ForegroundColor Green
