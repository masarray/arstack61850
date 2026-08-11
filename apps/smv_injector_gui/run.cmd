@echo off
setlocal
set "PORT=8765"
set "APP_DIR=%~dp0"
for %%I in ("%APP_DIR%..\..\..") do set "REPO_ROOT=%%~fI"
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
  echo Run this launcher from the ESP-IDF shell.
  pause
  exit /b 1
)

where ninja >nul 2>nul
if errorlevel 1 (
  echo Ninja was not found on PATH.
  echo Run this launcher from the ESP-IDF shell.
  pause
  exit /b 1
)

if not exist "%APP_DIR%index.html" (
  echo ERROR: GUI index.html was not found next to run.cmd.
  pause
  exit /b 1
)

echo ARStack61850 SMV Injector GUI
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

echo Local UI: http://127.0.0.1:%PORT%/
echo Smart SCL engine: %PROFILE_TOOL%
echo.
echo IMPORTANT: close idf.py monitor first so the GUI can own the serial port.
echo Press Ctrl+C in this window to stop the local control-plane server.
echo.

start "" "http://127.0.0.1:%PORT%/"
python "%APP_DIR%host_server.py" --profile-tool "%PROFILE_TOOL%" --port %PORT%
exit /b %ERRORLEVEL%

:build_failed
echo.
echo ERROR: smart SCL profile engine build failed.
pause
exit /b 1
