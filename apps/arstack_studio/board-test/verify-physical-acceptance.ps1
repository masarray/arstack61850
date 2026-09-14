param(
    [Parameter(Mandatory = $true)]
    [string]$Rc1Evidence,

    [Parameter(Mandatory = $true)]
    [string]$Rc2Evidence,

    [string]$Evaluator = ".\arstack_rc2_acceptance.exe",

    [string]$ExpectedSha = "ed46f3bed73d398d71be2eacf34c4995325c3cad"
)

$ErrorActionPreference = 'Stop'

function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$rc1Path = (Resolve-Path $Rc1Evidence).Path
$rc2Path = (Resolve-Path $Rc2Evidence).Path
$evaluatorPath = (Resolve-Path $Evaluator).Path

$rc1 = Get-Content -Raw $rc1Path | ConvertFrom-Json
Require ($rc1.schema -eq 'arstack.studio.rc1.session.v1') 'Unexpected RC1 evidence schema.'
Require ($rc1.candidateSha -eq $ExpectedSha) "RC1 evidence is tied to $($rc1.candidateSha), expected $ExpectedSha."
Require ($rc1.pass -eq $true) 'RC1 physical session evidence is not PASS.'
Require ($rc1.coldOpenCycles.Count -eq 10) 'RC1 evidence does not contain exactly 10 cold/open cycles.'
foreach ($cycle in $rc1.coldOpenCycles) {
    Require ($cycle.pass -eq $true) "RC1 cold/open cycle $($cycle.cycle) is not PASS."
}
Require ($rc1.readyUnplugReplug.pass -eq $true) 'READY unplug/replug is not PASS.'
Require ($rc1.runningUnplugReplug.pass -eq $true) 'RUNNING unplug/replug is not PASS.'
Require ($rc1.wrongDeviceRecovery.executed -eq $true -and $rc1.wrongDeviceRecovery.pass -eq $true) 'Wrong-device recovery was not executed and passed.'
Require ($rc1.firmwareHandoff.pass -eq $true) 'Firmware ownership handoff is not PASS.'
Require ($rc1.singleInstanceHardCrash.pass -eq $true) 'Single-instance/hard-crash recovery is not PASS.'

Write-Host 'RC1 physical session evidence: PASS' -ForegroundColor Green

& $evaluatorPath --evidence $rc2Path --expect-sha $ExpectedSha
$rc2Code = $LASTEXITCODE
if ($rc2Code -ne 0) {
    throw "RC2 fail-closed evaluator rejected physical evidence (exit code $rc2Code)."
}

Write-Host 'RC2 physical functional/performance evidence: PASS' -ForegroundColor Green
Write-Host "PHYSICAL RELEASE ACCEPTANCE: PASS · $ExpectedSha" -ForegroundColor Green
Write-Host 'This verdict is evidence for issue #83; it does not merge PR #79 or create a release tag.'
