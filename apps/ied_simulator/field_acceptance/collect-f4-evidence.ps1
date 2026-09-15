param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('IEDScoutToARStack', 'ARStackToIED')]
    [string]$Mode,

    [Parameter(Mandatory = $true)]
    [string]$TargetIp,

    [int]$Port = 102,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, 999)]
    [int]$InterfaceId,

    [ValidateRange(30, 1800)]
    [int]$DurationSeconds = 180,

    [string]$OutputRoot = (Join-Path $PWD 'f4-evidence')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-Dumpcap {
    $command = Get-Command dumpcap.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $candidate = Join-Path ${env:ProgramFiles} 'Wireshark\dumpcap.exe'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    throw 'dumpcap.exe not found. Install Wireshark/Npcap before F4 external acceptance.'
}

function Resolve-Tshark {
    $command = Get-Command tshark.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $candidate = Join-Path ${env:ProgramFiles} 'Wireshark\tshark.exe'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    return $null
}

if ($Port -lt 1 -or $Port -gt 65535) {
    throw 'Port must be in the range 1..65535.'
}

$dumpcap = Resolve-Dumpcap
$tshark = Resolve-Tshark
$stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmssZ')
$evidenceDir = Join-Path $OutputRoot ("F4-{0}-{1}" -f $Mode, $stamp)
New-Item -ItemType Directory -Force -Path $evidenceDir | Out-Null

$transcriptPath = Join-Path $evidenceDir 'collector-transcript.txt'
Start-Transcript -Path $transcriptPath -Force | Out-Null
try {
    Write-Host "F4 evidence collection"
    Write-Host "Mode: $Mode"
    Write-Host "Target: ${TargetIp}:$Port"
    Write-Host "Interface id: $InterfaceId"
    Write-Host "Duration: $DurationSeconds seconds"

    $interfacesPath = Join-Path $evidenceDir 'capture-interfaces.txt'
    & $dumpcap -D | Tee-Object -FilePath $interfacesPath | Out-Host

    $buildHead = 'unknown'
    $headFile = Join-Path $PSScriptRoot 'BUILD_HEAD.txt'
    if (Test-Path -LiteralPath $headFile -PathType Leaf) {
        $buildHead = (Get-Content -LiteralPath $headFile -Raw).Trim()
    }

    $preflight = Test-NetConnection -ComputerName $TargetIp -Port $Port -WarningAction SilentlyContinue
    $preflight | Format-List * | Out-String | Set-Content -LiteralPath (Join-Path $evidenceDir 'tcp-preflight.txt')
    if (-not $preflight.TcpTestSucceeded) {
        throw "TCP preflight failed for ${TargetIp}:$Port. Start the ARStack server or make the real IED reachable before collecting F4 evidence."
    }

    $pcapPath = Join-Path $evidenceDir 'mms-session.pcapng'
    $filter = "tcp port $Port"
    Write-Host "Starting dumpcap. Execute the F4 acceptance sequence now."
    Write-Host "Capture filter: $filter"

    $argumentLine = "-q -i $InterfaceId -f `"$filter`" -a duration:$DurationSeconds -w `"$pcapPath`""
    $capture = Start-Process -FilePath $dumpcap -ArgumentList $argumentLine -PassThru -NoNewWindow
    $capture.WaitForExit()
    if ($capture.ExitCode -ne 0) {
        throw "dumpcap failed with exit code $($capture.ExitCode)."
    }
    if (-not (Test-Path -LiteralPath $pcapPath -PathType Leaf)) {
        throw 'Capture completed without producing a PCAPNG file.'
    }

    $pcapInfo = Get-Item -LiteralPath $pcapPath
    if ($pcapInfo.Length -le 24) {
        throw "PCAPNG is unexpectedly small ($($pcapInfo.Length) bytes); external acceptance evidence is incomplete."
    }

    $pcapHash = (Get-FileHash -LiteralPath $pcapPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $packetCount = $null
    $tcpResetCount = $null
    if ($tshark) {
        $packetLines = & $tshark -r $pcapPath -Y "tcp.port==$Port" -T fields -e frame.number 2>$null
        $packetCount = @($packetLines | Where-Object { $_ -ne '' }).Count
        $resetLines = & $tshark -r $pcapPath -Y "tcp.port==$Port && tcp.flags.reset==1" -T fields -e frame.number 2>$null
        $tcpResetCount = @($resetLines | Where-Object { $_ -ne '' }).Count
    }

    $metadata = [ordered]@{
        schemaVersion = 'arstack-f4-evidence-v1'
        buildHead = $buildHead
        mode = $Mode
        targetIp = $TargetIp
        port = $Port
        interfaceId = $InterfaceId
        captureDurationSeconds = $DurationSeconds
        capturedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
        tcpPreflightSucceeded = [bool]$preflight.TcpTestSucceeded
        pcapFile = $pcapInfo.Name
        pcapBytes = [uint64]$pcapInfo.Length
        pcapSha256 = $pcapHash
        tcp102PacketCount = $packetCount
        tcpResetCount = $tcpResetCount
        externalAcceptance = 'pending_manual_review'
    }
    $metadata | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $evidenceDir 'evidence.json') -Encoding UTF8

    $template = Join-Path $PSScriptRoot 'ACCEPTANCE_RESULT_TEMPLATE.md'
    if (Test-Path -LiteralPath $template -PathType Leaf) {
        Copy-Item -LiteralPath $template -Destination (Join-Path $evidenceDir 'ACCEPTANCE_RESULT.md') -Force
    }

    Write-Host "F4 capture complete: $evidenceDir"
    Write-Host "PCAP SHA-256: $pcapHash"
    if ($null -ne $packetCount) { Write-Host "TCP/$Port packets: $packetCount" }
    if ($null -ne $tcpResetCount) { Write-Host "TCP resets: $tcpResetCount" }
    Write-Host 'Complete ACCEPTANCE_RESULT.md and retain Workbench/server diagnostics with this folder.'
}
finally {
    try { Stop-Transcript | Out-Null } catch { }
}
