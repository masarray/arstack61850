@echo off
setlocal
set "PORT=8765"

where python >nul 2>nul
if errorlevel 1 (
  echo Python was not found on PATH.
  echo Run this launcher from the ESP-IDF shell or another shell with Python available.
  pause
  exit /b 1
)

if not exist "%~dp0index.html" (
  echo ERROR: GUI index.html was not found next to run.cmd.
  echo Expected: %~dp0index.html
  pause
  exit /b 1
)

rem Serve from the launcher directory itself. Do not pass %%~dp0 to
rem http.server --directory: %%~dp0 ends in a backslash on Windows and can be
rem mis-parsed by the child process command-line quoting, causing HTTP 404s.
pushd "%~dp0"
if errorlevel 1 (
  echo ERROR: could not enter GUI directory: %~dp0
  pause
  exit /b 1
)

echo ARStack61850 SMV Injector GUI
echo Local UI: http://127.0.0.1:%PORT%/
echo Serving from: %CD%
echo.
echo IMPORTANT: close idf.py monitor first so the GUI can own the serial port.
echo Press Ctrl+C in this window to stop the local UI server.
echo.

start "" "http://127.0.0.1:%PORT%/"
python -m http.server %PORT% --bind 127.0.0.1
set "SERVER_EXIT=%ERRORLEVEL%"

popd
exit /b %SERVER_EXIT%
