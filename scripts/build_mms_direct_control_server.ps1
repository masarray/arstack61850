param(
    [string]$BuildDir = "build-mms-direct-control"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Source = Join-Path $Root "tools\mms_direct_control_server"
$Build = Join-Path $Root $BuildDir

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE."
    }
}

function Repair-MissingTrackedFiles {
    $insideWorkTree = (& git -C $Root rev-parse --is-inside-work-tree 2>$null)
    if ($LASTEXITCODE -ne 0 -or $insideWorkTree -ne "true") {
        return
    }

    $tracked = @(& git -C $Root ls-files)
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to enumerate tracked files before the MMS server build."
    }

    $missing = @(
        $tracked | Where-Object {
            $_ -and -not (Test-Path -LiteralPath (Join-Path $Root $_))
        }
    )

    if ($missing.Count -eq 0) {
        return
    }

    Write-Host "[repair] Restoring $($missing.Count) tracked file(s) missing from the working tree..."
    foreach ($relativePath in $missing) {
        Write-Host "[repair]   $relativePath"
    }

    & git -C $Root restore --worktree --source=HEAD -- @missing
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to restore tracked files missing from the working tree. Run 'git status --short' and inspect the checkout before retrying."
    }
}

Repair-MissingTrackedFiles

$requiredPaths = @(
    "CMakeLists.txt",
    "src\mms\association_runtime_control_wait.cpp",
    "src\mms\static_report_session.cpp",
    "src\control\guarded_control.cpp",
    "src\control\mms_control_structure.cpp",
    "src\control\command_termination.cpp",
    "src\control\control_discovery.cpp",
    "src\control\control_session.cpp",
    "src\control\mms_association_control_transport.cpp",
    "src\mms\static_direct_control.cpp",
    "src\mms\static_information_report.cpp",
    "src\mms\static_report_connection.cpp",
    "src\mms\static_urcb_objects.cpp",
    "src\mms\static_urcb_runtime.cpp",
    "tools\mms_direct_control_server.cpp",
    "tools\mms_urcb_server.cpp",
    "tools\mms_direct_control_server\CMakeLists.txt"
)

$stillMissing = @(
    $requiredPaths | Where-Object {
        -not (Test-Path -LiteralPath (Join-Path $Root $_))
    }
)
if ($stillMissing.Count -ne 0) {
    $formatted = ($stillMissing | ForEach-Object { "  - $_" }) -join [Environment]::NewLine
    throw "MMS server build prerequisites are still missing after checkout repair:$([Environment]::NewLine)$formatted$([Environment]::NewLine)Verify that HEAD is agent/iedsim-wire-parity-current and run 'git status --short'."
}

Write-Host "[OK] MMS R1-R2 current-lineage source preflight passed."

Invoke-Checked cmake -S $Source -B $Build -DCMAKE_BUILD_TYPE=Release
Invoke-Checked cmake --build $Build --config Release --parallel
Invoke-Checked ctest --test-dir $Build -C Release --output-on-failure

function Find-BuiltExe([string]$Name) {
    $candidates = @(
        (Join-Path $Build "Release\$Name.exe"),
        (Join-Path $Build "$Name.exe")
    )
    return $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}

$controlExe = Find-BuiltExe "ariec61850_mms_direct_control_server"
$urcbExe = Find-BuiltExe "ariec61850_mms_urcb_server"
if (-not $controlExe) {
    throw "Build succeeded but ariec61850_mms_direct_control_server.exe was not found."
}
if (-not $urcbExe) {
    throw "Build succeeded but ariec61850_mms_urcb_server.exe was not found."
}

Write-Host "[OK] MMS Direct Control server: $controlExe"
Write-Host "[OK] MMS URCB Reporting server: $urcbExe"
