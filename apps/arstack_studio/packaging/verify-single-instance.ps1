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

function Wait-ForExitBounded([System.Diagnostics.Process]$Process, [int]$TimeoutMs, [string]$Label) {
    if (-not $Process.WaitForExit($TimeoutMs)) {
        try { $Process.Kill() } catch {}
        try { $Process.WaitForExit(2000) | Out-Null } catch {}
        throw "$Label timed out after $TimeoutMs ms."
    }

    # PowerShell/.NET may leave ExitCode unmaterialized after the timed overload,
    # especially for quickly exiting GUI-subsystem processes. Complete the wait
    # and refresh the process snapshot before callers inspect ExitCode.
    $Process.WaitForExit()
    $Process.Refresh()
}

$first = $null
try {
    $first = Start-Process -FilePath $exe -PassThru
    $deadline = [DateTime]::UtcNow.AddMilliseconds($ReadyTimeoutMs)
    while (-not (Test-Path $lockPath) -and [DateTime]::UtcNow -lt $deadline) {
        if ($first.HasExited) {
            $first.Refresh()
            throw "Primary Studio instance exited before acquiring its process lock (exit=$($first.ExitCode))."
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not (Test-Path $lockPath)) {
        throw 'Primary Studio instance did not publish the single-instance lock in time.'
    }

    $second = Start-Process -FilePath $exe -PassThru
    Wait-ForExitBounded $second $ExitTimeoutMs 'Second Studio instance'
    if ($second.ExitCode -ne 23) {
        throw "Second Studio instance was not blocked by ownership lock; exit=$($second.ExitCode), expected=23."
    }

    # Kill the owner deliberately to exercise QLockFile stale-owner recovery,
    # matching a hard application crash rather than the graceful destructor path.
    $first.Kill()
    Wait-ForExitBounded $first $ExitTimeoutMs 'Primary Studio hard-stop'
    $first = $null

    $third = Start-Process -FilePath $exe -ArgumentList @('--check-app-lifecycle') -PassThru
    Wait-ForExitBounded $third $ExitTimeoutMs 'Post-crash Studio lifecycle'
    if ($third.ExitCode -ne 0) {
        throw "Studio did not reclaim the stale ownership lock after hard-stop; exit=$($third.ExitCode)."
    }

    Write-Host 'S8B packaged ownership: PASS · second instance exit 23 + stale-lock recovery + lifecycle'
} finally {
    if ($null -ne $first -and -not $first.HasExited) {
        try { $first.Kill() } catch {}
        try {
            if ($first.WaitForExit(2000)) {
                $first.WaitForExit()
                $first.Refresh()
            }
        } catch {}
    }
    Remove-Item $lockPath -Force -ErrorAction SilentlyContinue
}
