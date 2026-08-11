@echo off
setlocal EnableExtensions
set "PORT=8765"
set "BUILD_ONLY=0"
if /I "%~1"=="--build-only" set "BUILD_ONLY=1"

set "APP_DIR=%~dp0"
rem apps\smv_injector_gui -> repository root is two levels up.
for %%I in ("%APP_DIR%..\..") do set "REPO_ROOT=%%~fI"
set "BUILD_DIR=%REPO_ROOT%\build-smv-gui"
set "PROFILE_TOOL=%BUILD_DIR%\ariec61850_smv_profile_inspect.exe"

where python >nul 2>nul
if errorlevel 1 (
  echo Python was not found on PATH.
  echo Run this launcher from the ESP-IDF shell or another shell with Python available.
  pause
  exit /b 1
)

where cmake >nul 2>nul
if errorlevel 1 (
  echo CMake was not found on PATH.
  echo Run this launcher from the ESP-IDF shell or another shell with CMake available.
  pause
  exit /b 1
)

where ninja >nul 2>nul
if errorlevel 1 (
  echo Ninja was not found on PATH.
  echo Run this launcher from the ESP-IDF shell or another shell with Ninja available.
  pause
  exit /b 1
)

if not exist "%APP_DIR%index.html" (
  echo ERROR: GUI index.html was not found next to run.cmd.
  pause
  exit /b 1
)

call :prepare_host_toolchain
if errorlevel 1 goto :host_compiler_missing

rem A previous configure may have captured ESP-IDF's cross clang. CMake caches
rem compiler identity, so discard only the small host-tool configure cache when
rem it points at an Espressif compiler. This never touches embedded sdkconfig.
if exist "%BUILD_DIR%\CMakeCache.txt" (
  findstr /I /C:"Espressif/tools/esp-clang" /C:"Espressif\tools\esp-clang" "%BUILD_DIR%\CMakeCache.txt" >nul 2>nul
  if not errorlevel 1 (
    echo Removing stale ESP-IDF cross-compiler cache...
    del /q "%BUILD_DIR%\CMakeCache.txt" >nul 2>nul
    if exist "%BUILD_DIR%\CMakeFiles" rmdir /s /q "%BUILD_DIR%\CMakeFiles"
  )
)

echo ARStack61850 SMV Injector GUI
echo Repository: %REPO_ROOT%
echo Native host compiler: %HOST_COMPILER_LABEL%
echo Building smart SCL profile engine...
cmake -S "%APP_DIR%host" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto :build_failed
cmake --build "%BUILD_DIR%" --target ariec61850_smv_profile_inspect
if errorlevel 1 goto :build_failed

if not exist "%PROFILE_TOOL%" (
  echo ERROR: profile compiler was not produced: %PROFILE_TOOL%
  pause
  exit /b 1
)

if "%BUILD_ONLY%"=="1" (
  echo Smart SCL profile engine build PASS.
  exit /b 0
)

echo Local UI: http://127.0.0.1:%PORT%/
echo Smart SCL engine: %PROFILE_TOOL%
echo.
echo IMPORTANT: close idf.py monitor first so the GUI can own the serial port.
echo Press Ctrl+C in this window to stop the local control-plane server.
echo.

start "" "http://127.0.0.1:%PORT%/"
python "%APP_DIR%host_server.py" --profile-tool "%PROFILE_TOOL%" --port %PORT%
exit /b %ERRORLEVEL%

:prepare_host_toolchain
rem ESP-IDF PowerShell intentionally exports an embedded clang toolchain. That
rem compiler cannot link a native Windows host executable. Clear compiler hints
rem first, then deliberately select a native Windows compiler.
set "CC="
set "CXX="
set "AR="
set "AS="
set "LD="
set "CFLAGS="
set "CXXFLAGS="
set "LDFLAGS="
set "HOST_COMPILER_LABEL="

where cl.exe >nul 2>nul
if not errorlevel 1 (
  set "CC=cl.exe"
  set "CXX=cl.exe"
  set "HOST_COMPILER_LABEL=MSVC cl.exe"
  exit /b 0
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  set "VSINSTALL="
  for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
  if defined VSINSTALL (
    if exist "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" (
      call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64 >nul 2>nul
      where cl.exe >nul 2>nul
      if not errorlevel 1 (
        set "CC=cl.exe"
        set "CXX=cl.exe"
        set "HOST_COMPILER_LABEL=MSVC x64 via Visual Studio Build Tools"
        exit /b 0
      )
    )
  )
)

where g++.exe >nul 2>nul
if not errorlevel 1 (
  where gcc.exe >nul 2>nul
  if not errorlevel 1 (
    set "CC=gcc.exe"
    set "CXX=g++.exe"
    set "HOST_COMPILER_LABEL=MinGW GCC/G++"
    exit /b 0
  )
)

exit /b 1

:host_compiler_missing
echo.
echo ERROR: no native Windows C++ compiler was found.
echo The ESP-IDF esp-clang compiler is an embedded cross compiler and cannot
 echo link the Windows host-side SCL profile tool.
echo.
echo Install Microsoft Visual Studio Build Tools with the Desktop development
 echo with C++ workload, then run this same command again. The launcher will
 echo discover it automatically; do not change the ESP32-P4 toolchain.
echo.
pause
exit /b 1

:build_failed
echo.
echo ERROR: smart SCL profile engine build failed.
echo Native compiler selected: %HOST_COMPILER_LABEL%
pause
exit /b 1
