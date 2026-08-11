param(
    [string]$QtRoot = 'D:\Qt\6.8.3\msvc2022_64',
    [string]$VisualStudioRoot = '',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [switch]$NoLaunch
)

$ErrorActionPreference = 'Stop'

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
    # Do not pin the launcher to a Visual Studio marketing release. vswhere
    # selects the newest installation that actually carries the x64 C++ tools,
    # including Visual Studio 2026 / v145 and prerelease channels.
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

# Import the complete Developer Command Prompt environment. Environment names
# on Windows are case-insensitive; treating PATH and Path differently can drop
# cl.exe from the process environment and make CMake report an unknown compiler.
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

# A failed compiler probe can leave CMAKE_CXX_COMPILER-NOTFOUND cached. Clean
# only that broken configure state; successful incremental build caches remain.
$cacheFile = Join-Path $buildDirectory 'CMakeCache.txt'
if ((Test-Path -LiteralPath $cacheFile) -and
    (Select-String -LiteralPath $cacheFile -Pattern 'CMAKE_CXX_COMPILER.*NOTFOUND' -Quiet)) {
    Remove-Item -LiteralPath $cacheFile -Force
    $cmakeFiles = Join-Path $buildDirectory 'CMakeFiles'
    if (Test-Path -LiteralPath $cmakeFiles) {
        Remove-Item -LiteralPath $cmakeFiles -Recurse -Force
    }
}

Write-Host "Visual Studio: $visualStudio"
Write-Host "MSVC compiler: $($compiler.Source)"
Write-Host "Qt: $QtRoot"

& $cmake -S $PSScriptRoot -B $buildDirectory -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_CXX_COMPILER=$($compiler.Source)" `
    "-DCMAKE_PREFIX_PATH=$QtRoot" `
    "-DCMAKE_BUILD_TYPE=$Configuration"
if ($LASTEXITCODE -ne 0) { throw "Qt configure failed with exit code $LASTEXITCODE." }

& $cmake --build $buildDirectory --target arstack_studio --parallel
if ($LASTEXITCODE -ne 0) { throw "Qt build failed with exit code $LASTEXITCODE." }

$executable = Join-Path $buildDirectory 'arstack_studio.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    throw "Build completed but '$executable' was not created."
}

if (-not $NoLaunch) {
    $env:PATH = (Join-Path $QtRoot 'bin') + [IO.Path]::PathSeparator + $env:PATH
    Start-Process -FilePath $executable -WorkingDirectory $buildDirectory
}

Write-Host "ARStack Studio ready: $executable"
