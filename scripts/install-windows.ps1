# ARStack Studio — current public Windows installer.
# Saves the verified Setup EXE before starting its normal interactive installer.
param([switch]$DownloadOnly)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$api = 'https://api.github.com/repos/masarray/arstack61850/releases/latest'
$headers = @{ 'User-Agent' = 'ARStackStudio-Installer'; 'Accept' = 'application/vnd.github+json' }
$release = Invoke-RestMethod -Uri $api -Headers $headers
if ($release.draft -or $release.prerelease) { throw 'No public stable release was returned.' }
$setup = @($release.assets | Where-Object { $_.name -match '(?i)(win|windows).*setup\.exe$' }) | Select-Object -First 1
if (-not $setup) { throw 'The latest release does not contain a Windows Setup EXE. Check the release page.' }
$folder = Join-Path $env:TEMP ('ARStackStudio-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $folder -Force | Out-Null
$target = Join-Path $folder $setup.name
try {
    Write-Host "Downloading ARStack Studio ($($release.tag_name))..."
    Invoke-WebRequest -Uri $setup.browser_download_url -OutFile $target
    $expected = ''
    if ($setup.digest -match '^sha256:([a-fA-F0-9]{64})$') { $expected = $Matches[1].ToLowerInvariant() }
    $sums = @($release.assets | Where-Object { $_.name -eq 'SHA256SUMS.txt' }) | Select-Object -First 1
    if (-not $expected -and $sums) {
        $sumPath = Join-Path $folder 'SHA256SUMS.txt'
        Invoke-WebRequest -Uri $sums.browser_download_url -OutFile $sumPath
        foreach ($line in (Get-Content $sumPath)) {
            if ($line -match '^([a-fA-F0-9]{64})\s+\*?(.+)$' -and $Matches[2] -eq $setup.name) {
                $expected = $Matches[1].ToLowerInvariant()
                break
            }
        }
    }
    if (-not $expected) { throw 'No published SHA-256 checksum for the installer; refusing to launch.' }
    $actual = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $expected) { throw 'Downloaded installer checksum mismatch; refusing to launch.' }
    Write-Host "Verified SHA-256: $actual"
    if ($DownloadOnly) {
        $destination = Join-Path (Get-Location) $setup.name
        Copy-Item -LiteralPath $target -Destination $destination -Force
        Write-Host "Saved: $destination"
    } else {
        Write-Host 'Opening Windows Setup...'
        $process = Start-Process -FilePath $target -Wait -PassThru
        if ($process.ExitCode -ne 0) { throw "Setup exited with code $($process.ExitCode)." }
    }
} finally {
    Remove-Item -LiteralPath $folder -Recurse -Force -ErrorAction SilentlyContinue
}
