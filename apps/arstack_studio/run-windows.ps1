param(
    [string]$QtRoot = 'D:\Qt\6.8.3\msvc2022_64',
    [string]$VisualStudioRoot = '',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [ValidateRange(1, 64)]
    [int]$Jobs = 2,
    [switch]$Clean,
    [switch]$NoLaunch
)

$ErrorActionPreference = 'Stop'

function Remove-BuildDirectory {
    param([string]$Path, [string]$Reason)
    if (Test-Path -LiteralPath $Path) {
        Write-Host "Resetting Qt build directory: $Reason" -ForegroundColor Yellow
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
}

function Show-BuildFailureContext {
    param([string]$LogFile)

    if (-not (Test-Path -LiteralPath $LogFile)) {
        return
    }

    Write-Host ""
    Write-Host "=== Build failure context ===" -ForegroundColor Red
    $patterns = @(
        'FAILED:',
        'fatal error',
        'error C[0-9]+',
        'error LNK[0-9]+',
        'qml.*error',
        'ninja: error',
        'error:'
    )
    $matches = Select-String -LiteralPath $LogFile -Pattern $patterns -CaseSensitive:$false -Context 2,4
    if ($matches) {
        $matches | Select-Object -Last 10 | ForEach-Object {
            $_.Context.PreContext
            $_.Line
            $_.Context.PostContext
            Write-Host "---"
        }
    } else {
        Get-Content -LiteralPath $LogFile -Tail 120
    }
    Write-Host "=== End failure context ==="
    Write-Host "Full build log: $LogFile" -ForegroundColor Yellow
}

if (-not (Test-Path -LiteralPath (Join-Path $QtRoot 'bin\Qt6Core.dll'))) {
    throw "Qt runtime was not found at '$QtRoot'. Install a Qt 6.x MSVC x64 kit with Qt SerialPort, or pass -QtRoot explicitly."
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer (vswhere.exe) was not found.'
}

if ($VisualStudioRoot) {
    $visualStudio = (Resolve-Path -LiteralPath $VisualStudioRoot).Path
} else {
    $visualStudio = & $vswhere -latest -prerelease -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    $visualStudio = $visualStudio | Select-Object -First 1
}

if (-not $visualStudio) {
    throw 'A Visual Studio installation with the C++ x64 toolchain was not found.'
}

$developerCommand = Join-Path $visualStudio 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $developerCommand)) {
    throw "Visual Studio developer command was not found at '$developerCommand'."
}

$environmentLines = & $env:ComSpec /d /s /c "call `"$developerCommand`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
if ($LASTEXITCODE -ne 0) {
    throw "Visual Studio developer environment failed with exit code $LASTEXITCODE."
}

foreach ($line in $environmentLines) {
    $separator = $line.IndexOf('=')
    if ($separator -gt 0) {
        $name = $line.Substring(0, $separator)
        $value = $line.Substring($separator + 1)
        [Environment]::SetEnvironmentVariable($name, $value, 'Process')
    }
}

$compiler = Get-Command cl.exe -ErrorAction SilentlyContinue
if (-not $compiler) {
    throw "MSVC cl.exe was not exposed by '$developerCommand'. Verify that Desktop development with C++ / MSVC x64 tools are installed in Visual Studio."
}

$ninjaCandidates = @(
    (Join-Path $visualStudio 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'),
    ((Get-Command ninja.exe -ErrorAction SilentlyContinue).Source)
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
$ninja = $ninjaCandidates | Select-Object -First 1
if (-not $ninja) {
    throw 'Ninja was not found. Install the Visual Studio CMake tools component or make ninja.exe available in PATH.'
}

$cmakeCandidates = @(
    ((Get-Command cmake.exe -ErrorAction SilentlyContinue).Source),
    (Join-Path $visualStudio 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
$cmake = $cmakeCandidates | Select-Object -First 1
if (-not $cmake) {
    throw 'CMake was not found. Install the Visual Studio CMake tools component or make cmake.exe available in PATH.'
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$buildDirectory = Join-Path $repoRoot 'build-arstack-studio-qt'
$logDirectory = Join-Path $repoRoot 'build-logs'
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$configureLog = Join-Path $logDirectory 'arstack-studio-configure.log'
$buildLog = Join-Path $logDirectory 'arstack-studio-build.log'

if ($Clean) {
    Remove-BuildDirectory -Path $buildDirectory -Reason '-Clean was requested'
}

$cacheFile = Join-Path $buildDirectory 'CMakeCache.txt'
if (Test-Path -LiteralPath $cacheFile) {
    $cacheText = Get-Content -LiteralPath $cacheFile -Raw

    if ($cacheText -match 'CMAKE_CXX_COMPILER.*NOTFOUND') {
        Remove-BuildDirectory -Path $buildDirectory -Reason 'the previous compiler probe was invalid'
    } else {
        $compilerMatch = [regex]::Match($cacheText, '(?m)^CMAKE_CXX_COMPILER(?::FILEPATH)??=(.+)$')
        if ($compilerMatch.Success) {
            $cachedCompiler = $compilerMatch.Groups[1].Value.Trim().Replace('/', '\')
            $activeCompiler = $compiler.Source.Replace('/', '\')
            if (-not $cachedCompiler.Equals($activeCompiler, [System.StringComparison]::OrdinalIgnoreCase)) {
                Remove-BuildDirectory -Path $buildDirectory -Reason "cached compiler '$cachedCompiler' differs from active compiler '$activeCompiler'"
            }
        }

        if (Test-Path -LiteralPath $cacheFile) {
            $generatorMatch = [regex]::Match($cacheText, '(?m)^CMAKE_GENERATOR:INTERNAL=(.+)$')
            if ($generatorMatch.Success -and $generatorMatch.Groups[1].Value.Trim() -ne 'Ninja') {
                Remove-BuildDirectory -Path $buildDirectory -Reason "cached generator '$($generatorMatch.Groups[1].Value.Trim())' is not Ninja"
            }
        }
    }
}

Write-Host "Visual Studio: $visualStudio"
Write-Host "MSVC compiler: $($compiler.Source)"
Write-Host "Qt: $QtRoot"
Write-Host "CMake: $cmake"
Write-Host "Ninja: $ninja"
Write-Host "Parallel jobs: $Jobs"
Write-Host "Configure log: $configureLog"
Write-Host "Build log: $buildLog"

Remove-Item -LiteralPath $configureLog -Force -ErrorAction SilentlyContinue
& $cmake -S $PSScriptRoot -B $buildDirectory -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_CXX_COMPILER=$($compiler.Source)" `
    "-DCMAKE_PREFIX_PATH=$QtRoot" `
    "-DCMAKE_BUILD_TYPE=$Configuration" 2>&1 | Tee-Object -FilePath $configureLog
$configureExitCode = $LASTEXITCODE
if ($configureExitCode -ne 0) {
    Write-Host "Full configure log: $configureLog" -ForegroundColor Yellow
    throw "Qt configure failed with exit code $configureExitCode."
}

Remove-Item -LiteralPath $buildLog -Force -ErrorAction SilentlyContinue
& $cmake --build $buildDirectory --target arstack_studio --parallel $Jobs --verbose 2>&1 | Tee-Object -FilePath $buildLog
$buildExitCode = $LASTEXITCODE
if ($buildExitCode -ne 0) {
    Show-BuildFailureContext -LogFile $buildLog
    throw "Qt build failed with exit code $buildExitCode. See '$buildLog'."
}

$executable = Join-Path $buildDirectory 'arstack_studio.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    throw "Build completed but '$executable' was not created."
}

if (-not $NoLaunch) {
    $env:PATH = (Join-Path $QtRoot 'bin') + [IO.Path]::PathSeparator + $env:PATH
    Start-Process -FilePath $executable -WorkingDirectory $buildDirectory
}

Write-Host "ARStack Studio ready: $executable" -ForegroundColor Green
