param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('ExternalClientToARStack', 'ARStackToIED')]
    [string]$Mode,

    [Parameter(Mandatory = $true)]
    [string]$TargetIp,

    [int]$Port = 102,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, 999)]
    [int]$InterfaceId,

    [ValidateRange(30, 1800)]
    [int]$DurationSeconds = 180,

    [string]$OutputRoot = (Join-Path $PWD 'f4-evidence'),

    # Mandatory for Workbench -> IED: provenance of the ACTUAL installed
    # or extracted application, not only the field-kit wrapper.
    [string]$WorkbenchDirectory = ''
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

# A capture host expression and Wireshark display filter require a literal
# IPv4 endpoint. Never pass arbitrary user text through BPF/tshark filters.
[System.Net.IPAddress]$parsedIp = $null
if (-not [System.Net.IPAddress]::TryParse($TargetIp, [ref]$parsedIp) -or
    $parsedIp.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork -or
    $parsedIp.Equals([System.Net.IPAddress]::Any) -or
    $parsedIp.Equals([System.Net.IPAddress]::Broadcast)) {
    throw 'TargetIp must be a concrete IPv4 address; DNS/filter expressions are not accepted.'
}
$TargetIp = $parsedIp.ToString()

$kitHeadFile = Join-Path $PSScriptRoot 'BUILD_HEAD.txt'
if (-not (Test-Path -LiteralPath $kitHeadFile -PathType Leaf)) {
    throw 'F4 kit BUILD_HEAD.txt is missing; package provenance cannot be established.'
}
$buildHead = (Get-Content -LiteralPath $kitHeadFile -Raw).Trim()
if ($buildHead -cnotmatch '^[0-9a-f]{40}$') {
    throw 'F4 kit BUILD_HEAD.txt is not an exact 40-character source SHA.'
}
$workbenchBuildHead = $null
$workbenchVerified = $false
if ($Mode -eq 'ARStackToIED' -and [string]::IsNullOrWhiteSpace($WorkbenchDirectory)) {
    throw 'ARStackToIED requires -WorkbenchDirectory pointing to the installed/extracted Workbench.'
}
if (-not [string]::IsNullOrWhiteSpace($WorkbenchDirectory)) {
    $verifiedDirectory = (Resolve-Path -LiteralPath $WorkbenchDirectory -ErrorAction Stop).Path
    $app = Join-Path $verifiedDirectory 'arstack_ied_simulator.exe'
    $installedHeadFile = Join-Path $verifiedDirectory 'BUILD_HEAD.txt'
    if (-not (Test-Path -LiteralPath $app -PathType Leaf) -or
        -not (Test-Path -LiteralPath $installedHeadFile -PathType Leaf)) {
        throw 'Workbench executable or BUILD_HEAD.txt missing in selected installed/portable directory.'
    }
    $workbenchBuildHead = (Get-Content -LiteralPath $installedHeadFile -Raw).Trim()
    if ($workbenchBuildHead -cne $buildHead) {
        throw "Field kit/Workbench source SHA mismatch: kit=$buildHead app=$workbenchBuildHead"
    }
    $workbenchVerified = $true
}

$dumpcap = Resolve-Dumpcap
$tshark = Resolve-Tshark
if (-not $tshark) {
    throw 'tshark.exe required to verify target IPv4 MMS packet evidence (install Wireshark).'
}
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
    $interfaces = @(& $dumpcap -D)
    if ($LASTEXITCODE -ne 0) { throw 'dumpcap interface enumeration failed.' }
    $interfaces | Set-Content -LiteralPath $interfacesPath -Encoding UTF8
    $interfaces | Out-Host
    if (-not ($interfaces | Where-Object { $_ -match "^\s*$InterfaceId\." })) {
        throw "Capture interface id $InterfaceId is not present in dumpcap -D."
    }
    $buildHead | Set-Content -LiteralPath (Join-Path $evidenceDir 'workbench_build_sha.txt') -Encoding ascii

    $preflight = Test-NetConnection -ComputerName $TargetIp -Port $Port -WarningAction SilentlyContinue
    $preflight | Format-List * | Out-String | Set-Content -LiteralPath (Join-Path $evidenceDir 'tcp-preflight.txt')
    if (-not $preflight.TcpTestSucceeded) {
        throw "TCP preflight failed for ${TargetIp}:$Port. Start the ARStack server or make the real IED reachable before collecting F4 evidence."
    }

    $pcapPath = Join-Path $evidenceDir 'mms-session.pcapng'
    $filter = "host $TargetIp and tcp port $Port"
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
    $displayFilter = "ip.addr==$TargetIp && tcp.port==$Port"
    $packetLines = @(& $tshark -r $pcapPath -Y $displayFilter -T fields -e frame.number 2>$null)
    if ($LASTEXITCODE -ne 0) { throw 'tshark could not decode/verify target MMS capture.' }
    $packetCount = @($packetLines | Where-Object { $_ -ne '' }).Count
    $resetFilter = "$displayFilter && tcp.flags.reset==1"
    $resetLines = @(& $tshark -r $pcapPath -Y $resetFilter -T fields -e frame.number 2>$null)
    if ($LASTEXITCODE -ne 0) { throw 'tshark could not inspect target TCP reset evidence.' }
    $tcpResetCount = @($resetLines | Where-Object { $_ -ne '' }).Count

    $metadata = [ordered]@{
        schemaVersion = 'arstack-f4-evidence-v2-target-bound'
        buildHead = $buildHead
        workbenchBuildHead = $workbenchBuildHead
        workbenchProvenanceVerified = $workbenchVerified
        captureFilter = $filter
        targetPacketsVerified = ($packetCount -gt 0)
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
    if ($packetCount -eq 0) {
        throw 'No on-wire MMS TCP packets from the target were captured; verdict must be INCOMPLETE.'
    }

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
