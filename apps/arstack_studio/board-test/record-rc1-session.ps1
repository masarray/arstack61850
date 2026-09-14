param(
    [string]$Output = ".\rc1-session-evidence.json",
    [string]$ExpectedSha = "ed46f3bed73d398d71be2eacf34c4995325c3cad"
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

function Read-NonEmpty([string]$Prompt) {
    while ($true) {
        $value = (Read-Host $Prompt).Trim()
        if ($value.Length -gt 0) { return $value }
        Write-Host 'A value is required.' -ForegroundColor Yellow
    }
}

Write-Host "RC1 physical session recorder for frozen candidate $ExpectedSha" -ForegroundColor Cyan
Write-Host 'Record only facts physically observed on the board/Windows candidate.'
Write-Host ''

$cycles = @()
$cyclePass = $true
for ($i = 1; $i -le 10; $i++) {
    Write-Host "Cold/open cycle $i / 10" -ForegroundColor Cyan
    $deviceId = Read-NonEmpty 'Observed device_id'
    $port = Read-NonEmpty 'Observed COM port'
    $bounded = Read-YesNo 'Reached READY or an actionable bounded terminal state (no unbounded CONNECTING/PREPARING)'
    $falseInstall = Read-YesNo 'Did a false Install Firmware prompt appear for current firmware'
    $currentReady = Read-YesNo 'Current firmware was semantically recognized as current'
    $orphan = Read-YesNo 'Did closing Studio leave an orphan Studio/worker/firmware process'
    $pass = $bounded -and (-not $falseInstall) -and $currentReady -and (-not $orphan)
    if (-not $pass) { $cyclePass = $false }
    $cycles += [ordered]@{
        cycle = $i
        deviceId = $deviceId
        port = $port
        boundedTerminalState = $bounded
        falseInstallPrompt = $falseInstall
        currentFirmwareRecognized = $currentReady
        orphanProcessObserved = $orphan
        pass = $pass
    }
    $cycleResult = if ($pass) { 'PASS' } else { 'FAIL' }
    Write-Host $cycleResult
    Write-Host ''
}

Write-Host 'READY unplug/replug' -ForegroundColor Cyan
$readySameDevice = Read-YesNo 'Same semantic device_id recovered after READY unplug/replug (COM renumber allowed)'
$readyStopped = Read-YesNo 'Recovered device remained STOPPED'
$readyPass = $readySameDevice -and $readyStopped

Write-Host 'RUNNING unplug/replug' -ForegroundColor Cyan
$runningDropped = Read-YesNo 'RUNNING/session dropped when USB was removed'
$runningRecovered = Read-YesNo 'Same board recovered to READY after replug'
$runningAutoStart = Read-YesNo 'Did output automatically Start after recovery'
$runningPass = $runningDropped -and $runningRecovered -and (-not $runningAutoStart)

Write-Host 'Wrong-device recovery' -ForegroundColor Cyan
$wrongExecuted = Read-YesNo 'Was a second injector available and the wrong-device recovery test executed'
$wrongRejected = $false
$correctRecovered = $false
if ($wrongExecuted) {
    $wrongRejected = Read-YesNo 'Different injector was rejected fail-closed during recovery'
    $correctRecovered = Read-YesNo 'Original intended injector could be recovered explicitly afterward'
}
$wrongPass = $wrongExecuted -and $wrongRejected -and $correctRecovered

Write-Host 'Firmware serial -> espflash -> serial handoff' -ForegroundColor Cyan
$stoppedBeforeFlash = Read-YesNo 'Output was STOPPED before firmware ownership handoff'
$noAccessDenied = Read-YesNo 'No Windows COM Access is denied / simultaneous ownership contention occurred'
$flashVerified = Read-YesNo 'Flash/reset/re-enumeration returned the current semantic identity'
$flashStopped = Read-YesNo 'Board returned STOPPED and did not auto-Start'
$firmwarePass = $stoppedBeforeFlash -and $noAccessDenied -and $flashVerified -and $flashStopped

Write-Host 'Single instance / hard crash' -ForegroundColor Cyan
$secondBlocked = Read-YesNo 'Second Studio process was blocked while primary owned the session'
$staleRecovered = Read-YesNo 'After hard-killing primary, a new Studio instance reclaimed the stale lock'
$comReacquired = Read-YesNo 'Board COM/session could be reacquired after hard crash'
$instancePass = $secondBlocked -and $staleRecovered -and $comReacquired

$allPass = $cyclePass -and $readyPass -and $runningPass -and $wrongPass -and $firmwarePass -and $instancePass

$evidence = [ordered]@{
    schema = 'arstack.studio.rc1.session.v1'
    candidateSha = $ExpectedSha
    recordedAtUtc = [DateTime]::UtcNow.ToString('o')
    coldOpenCycles = $cycles
    readyUnplugReplug = [ordered]@{
        sameDeviceRecovered = $readySameDevice
        recoveredStopped = $readyStopped
        pass = $readyPass
    }
    runningUnplugReplug = [ordered]@{
        runningDropped = $runningDropped
        sameBoardRecovered = $runningRecovered
        automaticStartObserved = $runningAutoStart
        pass = $runningPass
    }
    wrongDeviceRecovery = [ordered]@{
        executed = $wrongExecuted
        wrongDeviceRejected = $wrongRejected
        intendedDeviceRecovered = $correctRecovered
        pass = $wrongPass
    }
    firmwareHandoff = [ordered]@{
        stoppedBeforeFlash = $stoppedBeforeFlash
        accessDeniedObserved = (-not $noAccessDenied)
        semanticIdentityVerifiedAfterFlash = $flashVerified
        recoveredStopped = $flashStopped
        pass = $firmwarePass
    }
    singleInstanceHardCrash = [ordered]@{
        secondInstanceBlocked = $secondBlocked
        staleLockReclaimed = $staleRecovered
        comSessionReacquired = $comReacquired
        pass = $instancePass
    }
    pass = $allPass
}

$evidence | ConvertTo-Json -Depth 12 | Set-Content -Encoding UTF8 $Output
Write-Host ''
if ($allPass) {
    Write-Host "RC1 PHYSICAL SESSION: PASS -> $Output" -ForegroundColor Green
    exit 0
}

Write-Host "RC1 PHYSICAL SESSION: NOT ACCEPTED -> $Output" -ForegroundColor Red
Write-Host 'Any FAIL or not-executed mandatory case keeps the release blocked.' -ForegroundColor Yellow
exit 8
