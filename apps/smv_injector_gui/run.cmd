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

if not exist "%APP_DIR%host_server.py" (
  echo ERROR: GUI host_server.py is missing from the local working tree.
  echo Restore the GUI files from the current branch, then run this launcher again:
  echo   git -C "%REPO_ROOT%" restore --source=HEAD -- apps/smv_injector_gui
  pause
  exit /b 1
)

rem The tiny host CMake scaffold is tracked in Git, but a locally deleted tracked
rem directory can survive a fast-forward pull. Self-heal this generated-style
rem scaffold so the operator never has to restore one internal build file by hand.
if not exist "%APP_DIR%host\CMakeLists.txt" (
  echo Repairing missing local host build scaffold...
  if not exist "%APP_DIR%host" mkdir "%APP_DIR%host" >nul 2>nul
  > "%APP_DIR%host\CMakeLists.txt" echo cmake_minimum_required^(VERSION 3.20^)
  >> "%APP_DIR%host\CMakeLists.txt" echo project^(ARStackSmvGuiHost LANGUAGES CXX^)
  >> "%APP_DIR%host\CMakeLists.txt" echo.
  >> "%APP_DIR%host\CMakeLists.txt" echo set^(ARIEC61850_BUILD_TESTS OFF CACHE BOOL "" FORCE^)
  >> "%APP_DIR%host\CMakeLists.txt" echo set^(ARIEC61850_BUILD_TOOLS OFF CACHE BOOL "" FORCE^)
  >> "%APP_DIR%host\CMakeLists.txt" echo set^(ARIEC61850_BUILD_FUZZERS OFF CACHE BOOL "" FORCE^)
  >> "%APP_DIR%host\CMakeLists.txt" echo set^(ARIEC61850_ENABLE_SANITIZERS OFF CACHE BOOL "" FORCE^)
  >> "%APP_DIR%host\CMakeLists.txt" echo.
  >> "%APP_DIR%host\CMakeLists.txt" echo add_subdirectory^(${CMAKE_CURRENT_LIST_DIR}/../../.. arstack-core^)
  >> "%APP_DIR%host\CMakeLists.txt" echo.
  >> "%APP_DIR%host\CMakeLists.txt" echo add_executable^(ariec61850_smv_profile_inspect
  >> "%APP_DIR%host\CMakeLists.txt" echo     ${CMAKE_CURRENT_LIST_DIR}/../../../tools/smv_profile_inspect.cpp^)
  >> "%APP_DIR%host\CMakeLists.txt" echo target_link_libraries^(ariec61850_smv_profile_inspect PRIVATE ARIEC61850::core^)
  >> "%APP_DIR%host\CMakeLists.txt" echo target_compile_features^(ariec61850_smv_profile_inspect PRIVATE cxx_std_20^)
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
if defined VSINSTALL_USED echo Visual Studio: %VSINSTALL_USED%
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
rem first, then deliberately select any installed native Windows compiler.
set "CC="
set "CXX="
set "AR="
set "AS="
set "LD="
set "CFLAGS="
set "CXXFLAGS="
set "LDFLAGS="
set "HOST_COMPILER_LABEL="
set "VSINSTALL_USED="
set "VISUAL_STUDIO_FOUND="
set "VISUAL_STUDIO_PATH="

rem If the current shell already has native MSVC configured, use it directly.
where cl.exe >nul 2>nul
if not errorlevel 1 (
  set "CC=cl.exe"
  set "CXX=cl.exe"
  set "HOST_COMPILER_LABEL=MSVC cl.exe from current environment"
  exit /b 0
)

rem Discover Visual Studio by capability, not by product year. Include preview /
rem prerelease installations so newer Visual Studio releases work without any
rem launcher change. vswhere is installed by the Visual Studio Installer.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  set "VSINSTALL="
  for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
  if defined VSINSTALL (
    set "VISUAL_STUDIO_FOUND=1"
    set "VISUAL_STUDIO_PATH=%VSINSTALL%"
    if exist "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" (
      call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64 >nul 2>nul
      where cl.exe >nul 2>nul
      if not errorlevel 1 (
        set "CC=cl.exe"
        set "CXX=cl.exe"
        set "HOST_COMPILER_LABEL=MSVC x64 via installed Visual Studio"
        set "VSINSTALL_USED=%VSINSTALL%"
        exit /b 0
      )
    )
  )

  rem If Visual Studio exists but the VC workload query did not match, remember
  rem the installation so the error can ask to modify that existing install
  rem instead of recommending another Visual Studio version.
  if not defined VISUAL_STUDIO_FOUND (
    set "VSINSTALL="
    for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -prerelease -products * -property installationPath`) do set "VSINSTALL=%%I"
    if defined VSINSTALL (
      set "VISUAL_STUDIO_FOUND=1"
      set "VISUAL_STUDIO_PATH=%VSINSTALL%"
      if exist "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" (
        call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64 >nul 2>nul
        where cl.exe >nul 2>nul
        if not errorlevel 1 (
          set "CC=cl.exe"
          set "CXX=cl.exe"
          set "HOST_COMPILER_LABEL=MSVC x64 via installed Visual Studio"
          set "VSINSTALL_USED=%VSINSTALL%"
          exit /b 0
        )
      )
    )
  )
)

rem Final Visual Studio fallback if vswhere is unavailable: scan version-neutral
rem install roots, including future product-year folders.
for %%R in ("%ProgramFiles%\Microsoft Visual Studio" "%ProgramFiles(x86)%\Microsoft Visual Studio") do (
  if exist "%%~R" (
    for /d %%V in ("%%~R\*") do (
      for /d %%E in ("%%~fV\*") do (
        if exist "%%~fE\Common7\Tools\VsDevCmd.bat" (
          set "VISUAL_STUDIO_FOUND=1"
          set "VISUAL_STUDIO_PATH=%%~fE"
          call "%%~fE\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64 >nul 2>nul
          where cl.exe >nul 2>nul
          if not errorlevel 1 (
            set "CC=cl.exe"
            set "CXX=cl.exe"
            set "HOST_COMPILER_LABEL=MSVC x64 via installed Visual Studio"
            set "VSINSTALL_USED=%%~fE"
            exit /b 0
          )
        )
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
echo ERROR: no usable native Windows C++ compiler was found.
echo The ESP-IDF esp-clang compiler is an embedded cross compiler and cannot
 echo link the Windows host-side SCL profile tool.
echo.
if defined VISUAL_STUDIO_FOUND (
  echo Visual Studio was detected at:
  echo   %VISUAL_STUDIO_PATH%
  echo.
  echo Keep that existing Visual Studio installation. Open Visual Studio Installer,
  echo choose Modify for that installation, and enable Desktop development with C++
  echo ^(MSVC x64/x86 build tools plus a Windows SDK^). No specific Visual Studio
  echo product year is required by ARStack61850.
) else (
  echo No Visual Studio installation with native C++ tools was detected.
  echo Install or modify any supported Visual Studio edition with Desktop development
  echo with C++, or provide a native MinGW GCC/G++ toolchain on PATH. ARStack61850
  echo does not require a specific Visual Studio product year.
)
echo.
pause
exit /b 1

:build_failed
echo.
echo ERROR: smart SCL profile engine build failed.
echo Native compiler selected: %HOST_COMPILER_LABEL%
if defined VSINSTALL_USED echo Visual Studio: %VSINSTALL_USED%
pause
exit /b 1
