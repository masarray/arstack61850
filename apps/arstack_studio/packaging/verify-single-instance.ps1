param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,
    [int]$ReadyTimeoutMs = 5000,
    [int]$ExitTimeoutMs = 10000
)

$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path $Executable).Path
$lockPath = Join-Path ([IO.Path]::GetTempPath()) 'arstack-studio-single-instance.lock'
Remove-Item $lockPath -Force -ErrorAction SilentlyContinue

function Start-TrackedProcess([string]$FilePath, [string]$Arguments = '') {
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $FilePath
    $startInfo.Arguments = $Arguments
    $startInfo.UseShellExecute = $false

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Failed to start $FilePath"
    }
    return $process
}

function Wait-ForExitBounded([System.Diagnostics.Process]$Process, [int]$TimeoutMs, [string]$Label) {
    if (-not $Process.WaitForExit($TimeoutMs)) {
        try { $Process.Kill() } catch {}
        try { $Process.WaitForExit(2000) | Out-Null } catch {}
        throw "$Label timed out after $TimeoutMs ms."
    }
    $Process.WaitForExit()
}

$first = $null
try {
    $first = Start-TrackedProcess $exe
    $deadline = [DateTime]::UtcNow.AddMilliseconds($ReadyTimeoutMs)
    while (-not (Test-Path $lockPath) -and [DateTime]::UtcNow -lt $deadline) {
        if ($first.HasExited) {
            throw "Primary Studio instance exited before acquiring its process lock (exit=$($first.ExitCode))."
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not (Test-Path $lockPath)) {
        throw 'Primary Studio instance did not publish the single-instance lock in time.'
    }

    $second = Start-TrackedProcess $exe
    Wait-ForExitBounded $second $ExitTimeoutMs 'Second Studio instance'
    if ($second.ExitCode -ne 23) {
        throw "Second Studio instance was not blocked by ownership lock; exit=$($second.ExitCode), expected=23."
    }

    # Kill the owner deliberately to exercise QLockFile stale-owner recovery,
    # matching a hard application crash rather than the graceful destructor path.
    $first.Kill()
    Wait-ForExitBounded $first $ExitTimeoutMs 'Primary Studio hard-stop'
    $first = $null

    $third = Start-TrackedProcess $exe '--check-app-lifecycle'
    Wait-ForExitBounded $third $ExitTimeoutMs 'Post-crash Studio lifecycle'
    if ($third.ExitCode -ne 0) {
        throw "Studio did not reclaim the stale ownership lock after hard-stop; exit=$($third.ExitCode)."
    }

    Write-Host 'S8B packaged ownership: PASS · second instance exit 23 + stale-lock recovery + lifecycle'
} finally {
    if ($null -ne $first -and -not $first.HasExited) {
        try { $first.Kill() } catch {}
        try { $first.WaitForExit(2000) | Out-Null } catch {}
    }
    Remove-Item $lockPath -Force -ErrorAction SilentlyContinue
}
