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

!ifdef ARSTACK_PREVIEW
  !define PACKAGE_NAME "ARStack Studio Preview"
  !define PACKAGE_DIR "$LOCALAPPDATA\Programs\ARStack Studio Preview"
  !define PACKAGE_REG "Software\ARStack61850\ARStack Studio Preview"
  !define PACKAGE_UNINSTALL "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARStackStudioPreview"
  !define PACKAGE_MENU "ARStack Studio Preview"
!else
  !define PACKAGE_NAME "ARStack Studio"
  !define PACKAGE_DIR "$LOCALAPPDATA\Programs\ARStack Studio"
  !define PACKAGE_REG "Software\ARStack61850\ARStack Studio"
  !define PACKAGE_UNINSTALL "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARStackStudio"
  !define PACKAGE_MENU "ARStack Studio"
!endif
Name "${PACKAGE_NAME}"
OutFile "${OUTPUT_FILE}"
InstallDir "${PACKAGE_DIR}"
InstallDirRegKey HKCU "${PACKAGE_REG}" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 32

VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey "ProductName" "${PACKAGE_NAME}"
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

  WriteRegStr HKCU "${PACKAGE_REG}" "InstallDir" "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  CreateDirectory "$SMPROGRAMS\${PACKAGE_MENU}"
  CreateShortcut "$SMPROGRAMS\${PACKAGE_MENU}\${PACKAGE_NAME}.lnk" "$INSTDIR\ARStackStudio.exe"
  CreateShortcut "$SMPROGRAMS\${PACKAGE_MENU}\Uninstall ${PACKAGE_NAME}.lnk" "$INSTDIR\Uninstall.exe"

  WriteRegStr HKCU "${PACKAGE_UNINSTALL}" "DisplayName" "${PACKAGE_NAME}"
  WriteRegStr HKCU "${PACKAGE_UNINSTALL}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "${PACKAGE_UNINSTALL}" "Publisher" "ARStack61850"
  WriteRegStr HKCU "${PACKAGE_UNINSTALL}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${PACKAGE_UNINSTALL}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKCU "${PACKAGE_UNINSTALL}" "NoModify" 1
  WriteRegDWORD HKCU "${PACKAGE_UNINSTALL}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\${PACKAGE_MENU}\${PACKAGE_NAME}.lnk"
  Delete "$SMPROGRAMS\${PACKAGE_MENU}\Uninstall ${PACKAGE_NAME}.lnk"
  RMDir "$SMPROGRAMS\${PACKAGE_MENU}"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKCU "${PACKAGE_UNINSTALL}"
  DeleteRegKey HKCU "${PACKAGE_REG}"
SectionEnd
