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


; This migration is compiled only for the STABLE channel. Preview cannot
; remove or update the public NSIS installation. The old NSIS uninstaller
; recursively deletes {app}; do not execute it as part of a new installation.
; Instead, after verified Inno replacement succeeds, retire only its
; proven-owned old uninstall executable and ARP registration.
#if PREVIEW
; Preview upgrades use Inno's native same-AppId uninstall-log tracking.
#else
[Code]
const
  LegacyNsisUninstallKey =
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\ARStackStudio';

function VerifiedLegacyNsis: Boolean;
var
  OldName, OldVersion, OldDir, OldUninstall: String;
  ExpectedDir, ExpectedUninstall: String;
begin
  Result := False;
  if not RegKeyExists(HKCU, LegacyNsisUninstallKey) then
    Exit;
  if not RegQueryStringValue(HKCU, LegacyNsisUninstallKey,
       'DisplayName', OldName) then Exit;
  if not RegQueryStringValue(HKCU, LegacyNsisUninstallKey,
       'DisplayVersion', OldVersion) then Exit;
  if not RegQueryStringValue(HKCU, LegacyNsisUninstallKey,
       'InstallLocation', OldDir) then Exit;
  if not RegQueryStringValue(HKCU, LegacyNsisUninstallKey,
       'UninstallString', OldUninstall) then Exit;

  ExpectedDir := ExpandConstant('{localappdata}\Programs\ARStack Studio');
  ExpectedUninstall := '"' + AddBackslash(ExpectedDir) + 'Uninstall.exe"';
  Result :=
    (CompareText(OldName, 'ARStack Studio') = 0) and
    (CompareText(OldVersion, '0.1.1') = 0) and
    (CompareText(OldDir, ExpectedDir) = 0) and
    (CompareText(OldUninstall, ExpectedUninstall) = 0) and
    FileExists(AddBackslash(ExpectedDir) + 'Uninstall.exe') and
    FileExists(AddBackslash(ExpectedDir) + 'ARStackStudio.exe');
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if RegKeyExists(HKCU, LegacyNsisUninstallKey) and
     not VerifiedLegacyNsis then
  begin
    Result := 'An older ARStack Studio installation was detected, but its ' +
      'ownership, version or install path could not be verified. ' +
      'No files were changed. Please contact ARStack support for a safe upgrade.';
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  OldUninstaller: String;
begin
  if (CurStep <> ssPostInstall) or
     not RegKeyExists(HKCU, LegacyNsisUninstallKey) then
    Exit;
  if not VerifiedLegacyNsis then
    RaiseException('Legacy ARStack Studio identity changed during installation.');

  OldUninstaller := ExpandConstant('{app}\Uninstall.exe');
  if not DeleteFile(OldUninstaller) then
    RaiseException('Could not retire the verified old NSIS uninstaller.');

  if not RegDeleteKeyIncludingSubkeys(HKCU, LegacyNsisUninstallKey) then
    RaiseException('Could not remove the old NSIS uninstall registration.');

  Log('Verified NSIS v0.1.1 registry and old uninstaller retired after Inno setup.');
end;
#endif
