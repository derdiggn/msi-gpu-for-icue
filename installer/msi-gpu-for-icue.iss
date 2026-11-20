; SPDX-License-Identifier: GPL-2.0-or-later
;
; Inno Setup script for msi-gpu-for-icue-setup-<version>.exe.
; Build with:  .\package.ps1 -Installer   (needs Inno Setup 6, see docs/development.md)
;
; The setup only copies the files to Program Files and then runs the same
; install.ps1 / uninstall.ps1 that the zip package uses, so there is exactly
; one installation code path (certificate, signing, ACLs).
;
; Command line (besides the usual /SILENT, /VERYSILENT, /LOG=...):
;   /ICUEDIR="D:\Corsair\iCUE5"   iCUE 5 is not installed in the default folder
; Uninstaller: /VERYSILENT keeps settings.ini; interactively you are asked.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#define AppName "msi-gpu-for-icue"
#define RepoUrl "https://github.com/derdiggn/msi-gpu-for-icue"

[Setup]
; Never change AppId: it identifies the installation for upgrades and uninstall.
AppId={{E60C48C5-92DB-4282-99FA-0FFB0A47B9FF}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=msi-gpu-for-icue contributors
AppPublisherURL={#RepoUrl}
AppSupportURL={#RepoUrl}/issues
AppUpdatesURL={#RepoUrl}/releases
DefaultDirName={autopf}\{#AppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile=..\LICENSE
OutputDir=..\dist
OutputBaseFilename={#AppName}-setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupLogging=yes
CloseApplications=no
RestartApplications=no
UninstallDisplayName={#AppName} (iCUE plugin for MSI GeForce RTX 50)
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} setup

[Messages]
WelcomeLabel2=This will install [name/ver] on your computer.%n%nThe plugin lets Corsair iCUE 5 control the RGB lighting of MSI GeForce RTX 50 graphics cards.%n%niCUE will be closed during the installation. iCUE only loads signed plugins, so the setup signs the plugin with a one-time local certificate and deletes its private key right away.

[Files]
Source: "..\build\MsiGpuForIcue.dll"; DestDir: "{app}\build"; Flags: ignoreversion
Source: "..\build\gputest.exe"; DestDir: "{app}\build"; Flags: ignoreversion
Source: "..\images\*"; DestDir: "{app}\images"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\install.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\uninstall.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\settings.example.ini"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD_PARTY.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\SECURITY.md"; DestDir: "{app}"; Flags: ignoreversion

[InstallDelete]
; Pictures are replaced as a whole on upgrade.
Type: filesandordirs; Name: "{app}\images"

[Registry]
; Remembered for the uninstaller (HKLM: writable by administrators only).
Root: HKLM64; Subkey: "Software\{#AppName}"; ValueType: string; ValueName: "IcueDir"; ValueData: "{code:GetIcueDir}"; Flags: uninsdeletekey

[Run]
Filename: "{code:GetIcueExe}"; Description: "Start iCUE"; Flags: postinstall nowait skipifsilent runasoriginaluser

[UninstallDelete]
Type: files; Name: "{app}\install.log"
Type: files; Name: "{app}\uninstall.log"

[Code]
var
  InstallFailed: Boolean;

function SetEnvironmentVariable(lpName, lpValue: String): Boolean;
  external 'SetEnvironmentVariableW@kernel32.dll stdcall';

function GetIcueDir(Param: String): String;
begin
  Result := RemoveBackslashUnlessRoot(
    ExpandConstant('{param:ICUEDIR|{commonpf64}\Corsair\Corsair iCUE5 Software}'));
end;

function GetIcueExe(Param: String): String;
begin
  Result := AddBackslash(GetIcueDir('')) + 'iCUE.exe';
end;

// One command-line argument for powershell.exe -File. Arguments are bound as
// plain strings, never parsed as PowerShell code. Windows paths cannot contain
// '"'; a trailing backslash (drive root) is doubled so it cannot escape the
// closing quote.
function Arg(S: String): String;
begin
  if Pos('"', S) > 0 then
    RaiseException('Invalid character in path: ' + S);
  if (Length(S) > 0) and (S[Length(S)] = '\') then
    S := S + '\';
  Result := '"' + S + '"';
end;

// Runs a script from the application folder (Program Files, administrators
// only) with -File, so no path is ever interpreted as code. The script writes
// its own transcript to LogFile, also in the application folder. Module
// auto-loading is pinned to the system modules: the user's module folder is
// writable by unelevated processes.
function RunScript(Script, Args, LogFile: String): Boolean;
var
  ResultCode: Integer;
begin
  SetEnvironmentVariable('PSModulePath', ExpandConstant('{sys}\WindowsPowerShell\v1.0\Modules'));
  Log('Running ' + Script + ' ' + Args);
  Result := Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File ' + Arg(ExpandConstant('{app}\') + Script) +
    ' ' + Args + ' -LogFile ' + Arg(LogFile),
    ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
  Log(Script + ' exit code ' + IntToStr(ResultCode));
end;

function LogTail(LogFile: String): String;
var
  Lines: TArrayOfString;
  I: Integer;
begin
  Result := '';
  if LoadStringsFromFile(LogFile, Lines) then
    for I := GetArrayLength(Lines) - 8 to GetArrayLength(Lines) - 1 do
      if I >= 0 then
        Result := Result + Lines[I] + #13#10;
end;

function InitializeSetup(): Boolean;
begin
  Result := FileExists(GetIcueExe(''));
  if not Result then
    SuppressibleMsgBox('Corsair iCUE 5 was not found in' + #13#10 + GetIcueDir('') + #13#10#13#10 +
      'Install iCUE 5 first, or start the setup with /ICUEDIR="<folder of iCUE.exe>".',
      mbCriticalError, MB_OK, IDOK);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  LogFile: String;
begin
  if CurStep <> ssPostInstall then
    Exit;
  LogFile := ExpandConstant('{app}\install.log');
  WizardForm.StatusLabel.Caption := 'Signing and installing the plugin ...';
  if not RunScript('install.ps1', '-IcueDir ' + Arg(GetIcueDir('')), LogFile) then begin
    InstallFailed := True;
    SuppressibleMsgBox('The plugin could not be installed:' + #13#10#13#10 + LogTail(LogFile) + #13#10 +
      'Full log: ' + LogFile, mbCriticalError, MB_OK, IDOK);
  end;
end;

function GetCustomSetupExitCode(): Integer;
begin
  if InstallFailed then
    Result := 1
  else
    Result := 0;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  IcueDir, Args, LogFile: String;
begin
  if CurUninstallStep <> usUninstall then
    Exit;
  if not RegQueryStringValue(HKLM64, 'Software\{#AppName}', 'IcueDir', IcueDir) then
    IcueDir := ExpandConstant('{commonpf64}\Corsair\Corsair iCUE5 Software');
  // uninstall.ps1 removes the trusted certificates even if another step fails.
  Args := '-IcueDir ' + Arg(RemoveBackslashUnlessRoot(IcueDir));
  if not UninstallSilent and
     (MsgBox('Also remove your plugin settings (%ProgramData%\MsiGpuForIcue\settings.ini)?',
             mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES) then
    Args := Args + ' -RemoveData';
  LogFile := ExpandConstant('{app}\uninstall.log');
  if not RunScript('uninstall.ps1', Args, LogFile) then
    SuppressibleMsgBox('Removing the plugin from iCUE failed:' + #13#10#13#10 + LogTail(LogFile),
      mbError, MB_OK, IDOK);
end;
