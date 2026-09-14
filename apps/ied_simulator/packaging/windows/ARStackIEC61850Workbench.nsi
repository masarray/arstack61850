Unicode True

!ifndef STAGE_DIR
  !error "STAGE_DIR define is required"
!endif
!ifndef OUT_FILE
  !error "OUT_FILE define is required"
!endif

Name "ARStack IEC 61850 Workbench"
OutFile "${OUT_FILE}"
InstallDir "$PROGRAMFILES64\ARStack IEC 61850 Workbench"
InstallDirRegKey HKLM "Software\ARStack61850\IEC61850Workbench" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

VIProductVersion "0.2.0.0"
VIAddVersionKey /LANG=1033 "ProductName" "ARStack IEC 61850 Workbench"
VIAddVersionKey /LANG=1033 "CompanyName" "ARStack61850"
VIAddVersionKey /LANG=1033 "FileDescription" "IEC 61850 engineering workbench"
VIAddVersionKey /LANG=1033 "FileVersion" "0.2.0"
VIAddVersionKey /LANG=1033 "ProductVersion" "0.2.0"

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "ARStack IEC 61850 Workbench" SEC_MAIN
  SetOutPath "$INSTDIR"
  File /r "${STAGE_DIR}\*.*"

  WriteRegStr HKLM "Software\ARStack61850\IEC61850Workbench" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARStackIEC61850Workbench" "DisplayName" "ARStack IEC 61850 Workbench"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARStackIEC61850Workbench" "DisplayVersion" "0.2.0"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARStackIEC61850Workbench" "Publisher" "ARStack61850"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARStackIEC61850Workbench" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARStackIEC61850Workbench" "UninstallString" '"$INSTDIR\Uninstall.exe"'

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\ARStack IEC 61850 Workbench"
  CreateShortcut "$SMPROGRAMS\ARStack IEC 61850 Workbench\ARStack IEC 61850 Workbench.lnk" "$INSTDIR\arstack_ied_simulator.exe"
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\ARStack IEC 61850 Workbench\ARStack IEC 61850 Workbench.lnk"
  RMDir "$SMPROGRAMS\ARStack IEC 61850 Workbench"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ARStackIEC61850Workbench"
  DeleteRegKey HKLM "Software\ARStack61850\IEC61850Workbench"
  RMDir /r "$INSTDIR"
SectionEnd
