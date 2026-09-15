Unicode true

!include "MUI2.nsh"

!ifndef SOURCE_DIR
  !error "SOURCE_DIR must point to the staged ARStack Studio directory"
!endif
!ifndef OUTPUT_FILE
  !define OUTPUT_FILE "ARStack-Studio-Setup.exe"
!endif
!ifndef APP_VERSION
  !define APP_VERSION "0.1.0"
!endif

Name "ARStack Studio"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\ARStack Studio"
InstallDirRegKey HKCU "Software\ARStack61850\ARStack Studio" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 32

VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey "ProductName" "ARStack Studio"
VIAddVersionKey "CompanyName" "ARStack61850"
VIAddVersionKey "FileDescription" "IEC 61850 Sampled Values Injector / Generator"
VIAddVersionKey "FileVersion" "${APP_VERSION}"
VIAddVersionKey "ProductVersion" "${APP_VERSION}"

!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "ARStack Studio" SEC_MAIN
  SetOutPath "$INSTDIR"
  File /r "${SOURCE_DIR}\*"

  WriteRegStr HKCU "Software\ARStack61850\ARStack Studio" "InstallDir" "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  CreateDirectory "$SMPROGRAMS\ARStack Studio"
  CreateShortcut "$SMPROGRAMS\ARStack Studio\ARStack Studio.lnk" "$INSTDIR\ARStackStudio.exe"
  CreateShortcut "$SMPROGRAMS\ARStack Studio\Uninstall ARStack Studio.lnk" "$INSTDIR\Uninstall.exe"

  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARStackStudio" "DisplayName" "ARStack Studio"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARStackStudio" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARStackStudio" "Publisher" "ARStack61850"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARStackStudio" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARStackStudio" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARStackStudio" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARStackStudio" "NoRepair" 1
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\ARStack Studio\ARStack Studio.lnk"
  Delete "$SMPROGRAMS\ARStack Studio\Uninstall ARStack Studio.lnk"
  RMDir "$SMPROGRAMS\ARStack Studio"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARStackStudio"
  DeleteRegKey HKCU "Software\ARStack61850\ARStack Studio"
SectionEnd
