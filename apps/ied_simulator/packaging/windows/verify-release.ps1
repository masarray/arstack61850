param(
    [Parameter(Mandatory = $true)][string]$StageDir,
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [Parameter(Mandatory = $true)][string]$InstallerPath,
    [Parameter(Mandatory = $true)][string]$PortablePath,
    [Parameter(Mandatory = $true)][string]$ExpectedSourceSha,
    [Parameter(Mandatory = $true)][string]$FixturePath
)

$ErrorActionPreference = 'Stop'

function Assert-File([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label missing: $Path"
    }
}

function Assert-BuildHead([string]$Directory, [string]$Label, [string]$Expected) {
    $headPath = Join-Path $Directory 'BUILD_HEAD.txt'
    Assert-File $headPath "$Label source revision marker"
    $observed = (Get-Content -LiteralPath $headPath -Raw).Trim()
    if ($observed -cne $Expected) {
        throw "$Label source mismatch: packaged=$observed expected=$Expected"
    }
}

function Invoke-Checked([string]$FilePath, [string[]]$ArgumentList, [string]$Label) {
    $process = Start-Process -FilePath $FilePath -ArgumentList $ArgumentList -Wait -PassThru -NoNewWindow
    if ($process.ExitCode -ne 0) {
        throw "$Label failed with exit code $($process.ExitCode)"
    }
}

$stage = (Resolve-Path -LiteralPath $StageDir).Path
$build = (Resolve-Path -LiteralPath $BuildDir).Path
$installer = (Resolve-Path -LiteralPath $InstallerPath).Path
$portable = (Resolve-Path -LiteralPath $PortablePath).Path
$fixture = (Resolve-Path -LiteralPath $FixturePath).Path
$expectedHead = $ExpectedSourceSha.Trim().ToLowerInvariant()
if ($expectedHead -notmatch '^[0-9a-f]{40}

$app = Join-Path $stage 'arstack_ied_simulator.exe'
$server = Join-Path $stage 'ariec61850_ied_simulator_server.exe'
Assert-File $app 'Staged workbench executable'
Assert-File $server 'Staged MMS simulator helper'
Assert-File (Join-Path $stage 'Qt6Core.dll') 'Qt Core runtime'
Assert-File (Join-Path $stage 'Qt6Gui.dll') 'Qt GUI runtime'
Assert-File (Join-Path $stage 'Qt6Network.dll') 'Qt Network runtime'
Assert-File (Join-Path $stage 'Qt6Qml.dll') 'Qt QML runtime'
Assert-File (Join-Path $stage 'Qt6Quick.dll') 'Qt Quick runtime'
Assert-File (Join-Path $stage 'platforms\qwindows.dll') 'Qt Windows platform plugin'

Invoke-Checked $app @('--smoke-test') 'Staged package smoke test'

# Check the ZIP contents, not just its shared pre-package staging directory.
$portableDir = Join-Path $env:RUNNER_TEMP 'arstack-iec61850-portable'
if (Test-Path -LiteralPath $portableDir) {
    Remove-Item -LiteralPath $portableDir -Recurse -Force
}
Expand-Archive -LiteralPath $portable -DestinationPath $portableDir -Force
Assert-BuildHead $portableDir 'Portable ZIP' $expectedHead
$portableApp = Join-Path $portableDir 'arstack_ied_simulator.exe'
Assert-File $portableApp 'Portable workbench executable'
Assert-File (Join-Path $portableDir 'Qt6Core.dll') 'Portable Qt Core runtime'
Invoke-Checked $portableApp @('--smoke-test') 'Portable ZIP smoke test'

$hardeningQa = Join-Path $build 'ied_product_hardening_qa.exe'
$mmsQa = Join-Path $build 'ied_mms_client_workbench_qa.exe'
Assert-File $hardeningQa 'Product hardening QA executable'
Assert-File $mmsQa 'MMS reconnect QA executable'

$hardeningOutput = (& $hardeningQa 2>&1 | Out-String)
Write-Host $hardeningOutput
if ($LASTEXITCODE -ne 0) {
    throw "Product hardening QA failed with exit code $LASTEXITCODE"
}
if ($hardeningOutput -notmatch 'PRODUCT_HARDENING_PASS' -or
    $hardeningOutput -notmatch 'WINDOWS_RUNTIME_READINESS_PASS' -or
    $hardeningOutput -notmatch 'guidance=pass') {
    throw 'Windows Npcap/runtime readiness evidence marker missing.'
}

$mmsOutput = (& $mmsQa $fixture 2>&1 | Out-String)
Write-Host $mmsOutput
if ($LASTEXITCODE -ne 0) {
    throw "MMS reconnect QA failed with exit code $LASTEXITCODE"
}
if ($mmsOutput -notmatch 'MMS_CLIENT_RECONNECT_SOAK_PASS' -or
    $mmsOutput -notmatch 'cycles=24') {
    throw 'MMS reconnect soak evidence marker missing.'
}

$installDir = Join-Path $env:RUNNER_TEMP 'arstack-iec61850-installed'
if (Test-Path -LiteralPath $installDir) {
    Remove-Item -LiteralPath $installDir -Recurse -Force
}

Invoke-Checked $installer @('/S', "/D=$installDir") 'Silent NSIS install'
$installedApp = Join-Path $installDir 'arstack_ied_simulator.exe'
$installedServer = Join-Path $installDir 'ariec61850_ied_simulator_server.exe'
Assert-File $installedApp 'Installed workbench executable'
Assert-BuildHead $installDir 'Installed workbench' $expectedHead
Assert-File $installedServer 'Installed MMS simulator helper'
Assert-File (Join-Path $installDir 'Qt6Core.dll') 'Installed Qt Core runtime'
Assert-File (Join-Path $installDir 'platforms\qwindows.dll') 'Installed Qt Windows platform plugin'
Invoke-Checked $installedApp @('--smoke-test') 'Installed package smoke test'

Write-Host 'WINDOWS_RELEASE_PACKAGE_PASS staged_smoke=pass installed_smoke=pass qt_runtime=pass helper_server=pass npcap_guidance=pass installer=nsis portable=zip reconnect_cycles=24'
) {
    throw 'Expected source SHA must be a complete 40-character git commit hash.'
}
Assert-BuildHead $stage 'Staged workbench' $expectedHead

$app = Join-Path $stage 'arstack_ied_simulator.exe'
$server = Join-Path $stage 'ariec61850_ied_simulator_server.exe'
Assert-File $app 'Staged workbench executable'
Assert-File $server 'Staged MMS simulator helper'
Assert-File (Join-Path $stage 'Qt6Core.dll') 'Qt Core runtime'
Assert-File (Join-Path $stage 'Qt6Gui.dll') 'Qt GUI runtime'
Assert-File (Join-Path $stage 'Qt6Network.dll') 'Qt Network runtime'
Assert-File (Join-Path $stage 'Qt6Qml.dll') 'Qt QML runtime'
Assert-File (Join-Path $stage 'Qt6Quick.dll') 'Qt Quick runtime'
Assert-File (Join-Path $stage 'platforms\qwindows.dll') 'Qt Windows platform plugin'

Invoke-Checked $app @('--smoke-test') 'Staged package smoke test'

$hardeningQa = Join-Path $build 'ied_product_hardening_qa.exe'
$mmsQa = Join-Path $build 'ied_mms_client_workbench_qa.exe'
Assert-File $hardeningQa 'Product hardening QA executable'
Assert-File $mmsQa 'MMS reconnect QA executable'

$hardeningOutput = (& $hardeningQa 2>&1 | Out-String)
Write-Host $hardeningOutput
if ($LASTEXITCODE -ne 0) {
    throw "Product hardening QA failed with exit code $LASTEXITCODE"
}
if ($hardeningOutput -notmatch 'PRODUCT_HARDENING_PASS' -or
    $hardeningOutput -notmatch 'WINDOWS_RUNTIME_READINESS_PASS' -or
    $hardeningOutput -notmatch 'guidance=pass') {
    throw 'Windows Npcap/runtime readiness evidence marker missing.'
}

$mmsOutput = (& $mmsQa $fixture 2>&1 | Out-String)
Write-Host $mmsOutput
if ($LASTEXITCODE -ne 0) {
    throw "MMS reconnect QA failed with exit code $LASTEXITCODE"
}
if ($mmsOutput -notmatch 'MMS_CLIENT_RECONNECT_SOAK_PASS' -or
    $mmsOutput -notmatch 'cycles=24') {
    throw 'MMS reconnect soak evidence marker missing.'
}

$installDir = Join-Path $env:RUNNER_TEMP 'arstack-iec61850-installed'
if (Test-Path -LiteralPath $installDir) {
    Remove-Item -LiteralPath $installDir -Recurse -Force
}

Invoke-Checked $installer @('/S', "/D=$installDir") 'Silent NSIS install'
$installedApp = Join-Path $installDir 'arstack_ied_simulator.exe'
$installedServer = Join-Path $installDir 'ariec61850_ied_simulator_server.exe'
Assert-File $installedApp 'Installed workbench executable'
Assert-File $installedServer 'Installed MMS simulator helper'
Assert-File (Join-Path $installDir 'Qt6Core.dll') 'Installed Qt Core runtime'
Assert-File (Join-Path $installDir 'platforms\qwindows.dll') 'Installed Qt Windows platform plugin'
Invoke-Checked $installedApp @('--smoke-test') 'Installed package smoke test'

Write-Host "WINDOWS_RELEASE_PACKAGE_PASS staged_smoke=pass installed_smoke=pass portable_smoke=pass source_sha=$expectedHead qt_runtime=pass helper_server=pass npcap_guidance=pass installer=nsis portable=zip reconnect_cycles=24"
