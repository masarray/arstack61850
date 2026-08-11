param(
    [string]$QtRoot = 'D:\Qt\6.8.3\msvc2022_64',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [switch]$NoLaunch
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath (Join-Path $QtRoot 'bin\Qt6Core.dll'))) {
    throw "Qt runtime was not found at '$QtRoot'. Install Qt 6.8.3 MSVC 2022 with Qt SerialPort first."
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer (vswhere.exe) was not found.'
}

$visualStudio = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) {
    throw 'A Visual Studio installation with the C++ x64 toolchain was not found.'
}

$developerCommand = Join-Path $visualStudio 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $developerCommand)) {
    throw "Visual Studio developer command was not found at '$developerCommand'."
}

$environmentLines = & $env:ComSpec /s /c "`"$developerCommand`" -no_logo -arch=x64 -host_arch=x64 && set"
foreach ($line in $environmentLines) {
    $separator = $line.IndexOf('=')
    if ($separator -gt 0) {
        [Environment]::SetEnvironmentVariable($line.Substring(0, $separator), $line.Substring($separator + 1), 'Process')
    }
}

$ninja = Join-Path $visualStudio 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
if (-not (Test-Path -LiteralPath $ninja)) {
    throw "The Visual Studio Ninja executable was not found at '$ninja'."
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$buildDirectory = Join-Path $repoRoot 'build-arstack-studio-qt'

& cmake -S $PSScriptRoot -B $buildDirectory -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_PREFIX_PATH=$QtRoot" `
    "-DCMAKE_BUILD_TYPE=$Configuration"
if ($LASTEXITCODE -ne 0) { throw "Qt configure failed with exit code $LASTEXITCODE." }

& cmake --build $buildDirectory --target arstack_studio --parallel
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
