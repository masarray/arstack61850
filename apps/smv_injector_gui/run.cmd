@echo off
setlocal
set "APP_DIR=%~dp0"
set "PORT=8765"

where python >nul 2>nul
if errorlevel 1 (
  echo Python was not found on PATH.
  echo Run this launcher from the ESP-IDF shell or another shell with Python available.
  pause
  exit /b 1
)

echo ARStack61850 SMV Injector GUI
echo Local UI: http://127.0.0.1:%PORT%/
echo.
echo IMPORTANT: close idf.py monitor first so the GUI can own the serial port.
echo Press Ctrl+C in this window to stop the local UI server.
echo.
start "" "http://127.0.0.1:%PORT%/"
python -m http.server %PORT% --bind 127.0.0.1 --directory "%APP_DIR%"
