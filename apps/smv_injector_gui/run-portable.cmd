@echo off
setlocal EnableExtensions
cd /d "%~dp0"
if not exist "%~dp0ariec61850_smv_profile_inspect.exe" (
  echo ERROR: The host binary profile compiler is missing from this package.
  pause
  exit /b 1
)
where python >nul 2>nul
if errorlevel 1 (
  echo ERROR: Python 3 is required on PATH for this localhost GUI.
  pause
  exit /b 1
)
echo ARStack61850 SMV Injector - bench binary-profile build
echo Close idf.py monitor before connecting the board.
echo Browser requirement: Microsoft Edge or Chrome with Web Serial.
echo.
start "" "http://127.0.0.1:8765/"
python "%~dp0host_server.py" --profile-tool "%~dp0ariec61850_smv_profile_inspect.exe" --port 8765
exit /b %ERRORLEVEL%
