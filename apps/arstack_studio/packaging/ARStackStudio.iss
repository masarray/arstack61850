; ARStack Studio Windows installer - Inno Setup 6
; Stable and Preview use separate AppId and install directories. Never run
; the old NSIS uninstaller implicitly: safe migration is separately gated.

#ifndef STAGE_DIR
  #error STAGE_DIR is mandatory
#endif
#ifndef PREVIEW
  #define PREVIEW 0
#endif
#ifndef STUDIO_VERSION
  #define STUDIO_VERSION "0.1.1"
#endif

#if PREVIEW
  #define ProductName "ARStack Studio Preview"
  #define ProductId "ARStack61850.Studio.Preview"
  #define ProductFolder "ARStack Studio Preview"
  #define ProductOutput "ARStack-Studio-Preview-win-x64-setup"
  #define ProductRegistry "Software\ARStack61850\ARStack Studio Preview"
#else
  #define ProductName "ARStack Studio"
  #define ProductId "ARStack61850.Studio.Stable"
  #define ProductFolder "ARStack Studio"
  #define ProductOutput "ARStack-Studio-win-x64-setup"
  #define ProductRegistry "Software\ARStack61850\ARStack Studio"
#endif

[Setup]
AppId={#ProductId}
AppName={#ProductName}
AppVerName={#ProductName} {#STUDIO_VERSION}
AppVersion={#STUDIO_VERSION}
AppPublisher=ARStack61850
AppPublisherURL=https://github.com/masarray/arstack61850
AppSupportURL=https://github.com/masarray/arstack61850/issues
AppUpdatesURL=https://github.com/masarray/arstack61850/releases
UninstallDisplayName={#ProductName}
DefaultDirName={localappdata}\Programs\{#ProductFolder}
DefaultGroupName={#ProductName}
OutputBaseFilename={#ProductOutput}
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
CloseApplications=yes
CloseApplicationsFilter=ARStackStudio.exe,*.dll
RestartApplications=no
SetupMutex=ARStackStudioInstaller
UsePreviousAppDir=yes
UsePreviousGroup=yes
DisableDirPage=auto
DisableProgramGroupPage=auto
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
SetupLogging=yes
UninstallLogging=yes
Uninstallable=yes
MinVersion=10.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#STAGE_DIR}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#ProductName}"; Filename: "{app}\ARStackStudio.exe"
Name: "{group}\Uninstall {#ProductName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#ProductName}"; Filename: "{app}\ARStackStudio.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Registry]
Root: HKCU; Subkey: "{#ProductRegistry}"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "{#ProductRegistry}"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"; Flags: uninsdeletevalue

[Run]
Filename: "{app}\ARStackStudio.exe"; Description: "Launch {#ProductName}"; Flags: nowait postinstall skipifsilent
