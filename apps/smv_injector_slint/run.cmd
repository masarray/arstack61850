@echo off
setlocal EnableExtensions

set "APP_DIR=%~dp0"
for %%I in ("%APP_DIR%\..\..") do set "REPO_ROOT=%%~fI"
set "BUILD_DIR=%REPO_ROOT%\build-smv-slint"
set "TARGET=arstack_smv_slint_shell"

where cmake >nul 2>nul
if errorlevel 1 (
    echo [ERROR] CMake 3.21 or newer is required.
    exit /b 1
)

where rustc >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Rust is required when Slint is fetched from source.
    echo         Install Rust 1.92 or newer, or install the Slint C++ SDK and configure CMAKE_PREFIX_PATH.
    exit /b 1
)

set "GENERATOR_ARGS="
set "EXE_PATH=%BUILD_DIR%\Release\%TARGET%.exe"
where ninja >nul 2>nul
if not errorlevel 1 (
    set "GENERATOR_ARGS=-G Ninja"
    set "EXE_PATH=%BUILD_DIR%\%TARGET%.exe"
)

echo [ARStack61850] Configuring native Slint shell...
cmake -S "%APP_DIR%" -B "%BUILD_DIR%" %GENERATOR_ARGS% -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1

echo [ARStack61850] Building %TARGET%...
cmake --build "%BUILD_DIR%" --target %TARGET% --config Release --parallel 2
if errorlevel 1 exit /b 1

if not exist "%EXE_PATH%" (
    echo [ERROR] Build succeeded but executable was not found at:
    echo         %EXE_PATH%
    exit /b 1
)

echo [ARStack61850] Starting SMV Workbench...
start "ARStack61850 SMV Workbench" "%EXE_PATH%"
exit /b 0
