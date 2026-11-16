; Inno Setup script for NBA Unrivaled (Windows 10/11, 64-bit).
; Modeled on NHL Hitz Pro's installer UX, without the texture-pack step.
; Game content comes from a player-supplied ZIP or extracted folder (not an ISO).
;
; Build portable + AVX player exes, config.exe, and nba_import first, then:
;   powershell -File installer\Prepare-Redist.ps1
;   "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" /DAppVersion=0.1.0 installer\nba_unrivaled.iss
; Setup CPU-detects at install time (PF_AVX). Primary game binary is always
; nbaunrivaled.exe (AVX when usable, else portable). The other ISA is also
; installed renamed: nbaunrivaled-NonAVX.exe or nbaunrivaled-AVX.exe.
; Shortcuts target nbaunrivaled.exe only. No runtime stub.
; Bundled VC++ x64 redist: detect missing VCRUNTIME140/MSVCP140, tell the user,
; then run the redistributable before nba_import (Inno [Run] is too late).
#ifndef AppVersion
#define AppVersion "1.0.0"
#endif
#ifndef OutSuffix
#define OutSuffix ""
#endif
#ifndef GameGuid
#define GameGuid "A7C3E91B-4D28-4F6A-9B12-8E5C0D47A2F1"
#endif
#ifndef GameShortcutName
#define GameShortcutName "NBA Unrivaled"
#endif
; BuildDir: config / import / DLLs. AvxBuildDir defaults to same (znver1 Release).
; PortableBuildDir: SSE4.1 floor (win-amd64-release-portable).
#ifndef BuildDir
#define BuildDir "..\out\build\win-amd64-release"
#endif
#ifndef PortableBuildDir
#define PortableBuildDir "..\out\build\win-amd64-release-portable"
#endif
#ifndef AvxBuildDir
; Keep a concrete path (not "{#BuildDir}") so ISCC /D overrides cannot leave a
; nested define unexpanded when building from PowerShell.
#define AvxBuildDir "..\out\build\win-amd64-release"
#endif
; Microsoft VC++ Redistributable x64 (run Prepare-Redist.ps1 before ISCC).
#ifndef VcRedistFile
#define VcRedistFile "redist\vc_redist.x64.exe"
#endif
#ifndef DefaultInstallDir
#define DefaultInstallDir "C:\Games\NBA Unrivaled"
#endif

[Setup]
AppId={{{#GameGuid}}
AppName={#GameShortcutName}
AppVersion={#AppVersion}
AppPublisher=NBA Unrivaled Recompiled
DefaultDirName={#DefaultInstallDir}
DefaultGroupName=NBA Unrivaled
DisableProgramGroupPage=yes
DisableDirPage=no
UsePreviousAppDir=yes
CloseApplications=yes
CloseApplicationsFilter=nbaunrivaled.exe,nbaunrivaled-AVX.exe,nbaunrivaled-NonAVX.exe,config.exe
; Non-elevated by default (C:\Games\… / per-user dirs, no UAC for the wizard).
; VC++ redist elevates alone via ShellExec('runas') only when CRT DLLs are missing.
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
SetupIconFile=..\resources\nba_unrivaled.ico
UninstallDisplayIcon={app}\nbaunrivaled.exe
OutputBaseFilename=NBAUnrivaled-Setup-{#AppVersion}{#OutSuffix}
OutputDir=Output
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchiveExtraction=enhanced/nopassword

[Messages]
WelcomeLabel2=This installs the NBA Unrivaled PC recompilation.%n%nThe game's own files are not included. On a first install you need your own Xbox 360 XBLA copy: prefer the Xbox LIVE package tree (title 584109D4 / 000D0000 / content file); an already-extracted folder with default.xex also works. A ZIP of either layout is fine. If the game is already under the install folder, Setup updates or repairs the program files and does not ask for the ZIP again. There is no texture-pack option.

[Files]
; Both ISA builds always install. Primary keeps the normal title name.
Source: "{#AvxBuildDir}\nbaunrivaled.exe"; DestDir: "{app}"; DestName: "nbaunrivaled.exe"; Flags: ignoreversion; Check: CpuHasAvx
Source: "{#PortableBuildDir}\nbaunrivaled.exe"; DestDir: "{app}"; DestName: "nbaunrivaled.exe"; Flags: ignoreversion; Check: not CpuHasAvx
; Unused ISA variant, renamed for optional manual testing.
Source: "{#PortableBuildDir}\nbaunrivaled.exe"; DestDir: "{app}"; DestName: "nbaunrivaled-NonAVX.exe"; Flags: ignoreversion; Check: CpuHasAvx
Source: "{#AvxBuildDir}\nbaunrivaled.exe"; DestDir: "{app}"; DestName: "nbaunrivaled-AVX.exe"; Flags: ignoreversion; Check: not CpuHasAvx
Source: "{#BuildDir}\config.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\rexruntime.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\rexgpu-xenos.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\nba_import.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\recomplauncher.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#VcRedistFile}"; DestDir: "{tmp}"; DestName: "vc_redist.x64.exe"; Flags: ignoreversion deleteafterinstall
Source: "..\scripts\Apply-TitleMenuPatches.ps1"; DestDir: "{app}\launcher-support"; Flags: ignoreversion
Source: "..\scripts\title-menu-patches.json"; DestDir: "{app}\launcher-support"; Flags: ignoreversion
Source: "Launch.cmd"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
; Launch.cmd owns --game_data_root. Pointing the
; shortcuts at the exe alone used to surface ReXGlue's
; "--game_data_root was not provided" dialog when args were lost.
Name: "{autoprograms}\{#GameShortcutName}"; Filename: "{app}\Launch.cmd"; WorkingDir: "{app}"; IconFilename: "{app}\nbaunrivaled.exe"
Name: "{autoprograms}\{#GameShortcutName} Settings"; Filename: "{app}\config.exe"; WorkingDir: "{app}"; IconFilename: "{app}\config.exe"
Name: "{autodesktop}\{#GameShortcutName}"; Filename: "{app}\Launch.cmd"; WorkingDir: "{app}"; IconFilename: "{app}\nbaunrivaled.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Run]
Filename: "{app}\Launch.cmd"; Description: "Launch NBA Unrivaled"; Flags: nowait postinstall skipifsilent unchecked

[UninstallDelete]
Type: filesandordirs; Name: "{app}\game"
Type: files; Name: "{app}\nbaunrivaled-AVX.exe"
Type: files; Name: "{app}\nbaunrivaled-NonAVX.exe"
; Leftovers from earlier stub / lowercase dual-exe experiments.
Type: files; Name: "{app}\nbaunrivaled-avx.exe"
Type: files; Name: "{app}\nba_launch.exe"

[Code]
var
  SrcPage, UpdatePage: TWizardPage;
  SrcEdit: TNewEdit;
  UpdateText: TNewStaticText;
  UpdateOnly: Boolean;
  OldDir, OldVer, NewVer: String;
  InstallFailed: Boolean;

function IsProcessorFeaturePresent(Feature: DWORD): BOOL;
  external 'IsProcessorFeaturePresent@kernel32.dll stdcall';

const
  PF_AVX_INSTRUCTIONS_AVAILABLE = 39;

function CpuHasAvx: Boolean;
begin
  { Install-time detect. Mirrors CPUID AVX + OSXSAVE/XCR0; no runtime stub. }
  Result := IsProcessorFeaturePresent(PF_AVX_INSTRUCTIONS_AVAILABLE);
end;

function HasExpectedSize(Path: String; ExpectedSize: Integer): Boolean;
var
  Size: Integer;
begin
  Result := FileSize(Path, Size) and (Size = ExpectedSize);
end;

function GameFilesPresent(Dir: String): Boolean;
var
  GameDir, Xex: String;
begin
  GameDir := AddBackslash(Dir) + 'game\';
  Xex := GameDir + 'default.xex';
  { Keep these markers aligned with the supported dump in checklist.h. }
  Result := HasExpectedSize(Xex, 1335296) and
            HasExpectedSize(GameDir + 'data.zip', 93854885) and
            HasExpectedSize(GameDir + 'data.idx', 292399) and
            HasExpectedSize(GameDir + 'ArcadeInfo.xml', 4246) and
            HasExpectedSize(GameDir + 'game_icon.png', 11871) and
            HasExpectedSize(GameDir + 'achievement01.png', 9620) and
            HasExpectedSize(GameDir + 'AUDIO\CROWD_LOOP1_V1.OGG', 5662444) and
            DirExists(GameDir + 'FRONT_END');
  if Result then
    Result := CompareText(GetSHA256OfFile(Xex),
      '93f1767b20bdf009bd091ecbf10e72197325ae7ce193c71b8ecbfaa0a4cde115') = 0;
end;

function PreviousAppDir: String;
var
  Key, Dir: String;
begin
  Result := '';
  Key := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{{#GameGuid}}_is1';
  if RegQueryStringValue(HKCU, Key, 'Inno Setup: App Path', Dir) or
     RegQueryStringValue(HKLM, Key, 'Inno Setup: App Path', Dir) then
    Result := RemoveBackslashUnlessRoot(Dir);
end;

function PreviousVersion: String;
var
  Key, V: String;
begin
  Result := '';
  Key := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{{#GameGuid}}_is1';
  if RegQueryStringValue(HKCU, Key, 'DisplayVersion', V) or
     RegQueryStringValue(HKLM, Key, 'DisplayVersion', V) then
    Result := V;
end;

function IsRegisteredDir(Dir: String): Boolean;
begin
  Result := (OldDir <> '') and (CompareText(RemoveBackslashUnlessRoot(Dir), OldDir) = 0);
end;

function VerPart(V: String; N: Integer): Integer;
var
  I, P: Integer;
  Piece: String;
begin
  for I := 1 to N do
  begin
    P := Pos('.', V);
    if P = 0 then begin V := ''; Break; end;
    V := Copy(V, P + 1, Length(V));
  end;
  P := Pos('.', V);
  if P > 0 then V := Copy(V, 1, P - 1);
  { Strip prerelease / TEST suffixes so 1.0.0-TEST compares as 1.0.0. }
  Piece := V;
  P := Pos('-', Piece);
  if P > 0 then Piece := Copy(Piece, 1, P - 1);
  Result := StrToIntDef(Piece, 0);
end;

function CompareVer(A, B: String): Integer;
var
  I: Integer;
begin
  if CompareText(A, B) = 0 then
  begin
    Result := 0;
    Exit;
  end;
  Result := 0;
  for I := 0 to 3 do
    if VerPart(A, I) <> VerPart(B, I) then
    begin
      if VerPart(A, I) < VerPart(B, I) then Result := -1 else Result := 1;
      Exit;
    end;
end;

function MaintenanceTitle(Dir: String): String;
var
  VersionOrder: Integer;
begin
  Result := 'Repair NBA Unrivaled';
  if IsRegisteredDir(Dir) and (OldVer <> '') then
  begin
    VersionOrder := CompareVer(OldVer, NewVer);
    if VersionOrder < 0 then Result := 'Update NBA Unrivaled';
    if VersionOrder > 0 then Result := 'Replace NBA Unrivaled with an older version';
  end;
end;

procedure BrowseFile(Sender: TObject);
var
  F: String;
begin
  F := SrcEdit.Text;
  if GetOpenFileName('Select the game ZIP', F, '', 'ZIP archives (*.zip)|*.zip|All files (*.*)|*.*', 'zip') then
    SrcEdit.Text := F;
end;

procedure BrowseFolder(Sender: TObject);
var
  D: String;
begin
  D := SrcEdit.Text;
  if BrowseForFolder('Select the LIVE package folder (…\584109D4) or extracted game folder (default.xex)', D, False) then
    SrcEdit.Text := D;
end;

procedure InitializeWizard;
var
  L: TNewStaticText;
  B1, B2: TNewButton;
begin
  NewVer := '{#AppVersion}';
  OldDir := PreviousAppDir;
  OldVer := PreviousVersion;
  UpdatePage := CreateCustomPage(wpSelectDir, 'Existing installation',
    'Review the selected folder before Setup continues.');
  UpdateText := TNewStaticText.Create(UpdatePage);
  UpdateText.Parent := UpdatePage.Surface;
  UpdateText.AutoSize := False;
  UpdateText.WordWrap := True;
  UpdateText.SetBounds(0, 0, UpdatePage.SurfaceWidth, UpdatePage.SurfaceHeight);
  SrcPage := CreateCustomPage(UpdatePage.ID, 'Your copy of the game',
    'Select the Xbox LIVE package (preferred) or extracted game folder / ZIP.');
  L := TNewStaticText.Create(SrcPage);
  L.Parent := SrcPage.Surface;
  L.Caption := 'LIVE package folder/ZIP (584109D4\000D0000\…) preferred; extracted default.xex also OK:';
  L.AutoSize := False;
  L.WordWrap := True;
  L.Left := 0; L.Top := 0;
  L.Width := SrcPage.SurfaceWidth;
  L.Height := ScaleY(36);
  SrcEdit := TNewEdit.Create(SrcPage);
  SrcEdit.Parent := SrcPage.Surface;
  SrcEdit.Left := 0; SrcEdit.Top := L.Height + 8;
  SrcEdit.Width := SrcPage.SurfaceWidth;
  SrcEdit.Text := ExpandConstant('{param:SOURCE|}');
  B1 := TNewButton.Create(SrcPage);
  B1.Parent := SrcPage.Surface;
  B1.Caption := 'Folder...';
  B1.Left := 0; B1.Top := SrcEdit.Top + SrcEdit.Height + 8;
  B1.Width := ScaleX(110);
  B1.OnClick := @BrowseFolder;
  B2 := TNewButton.Create(SrcPage);
  B2.Parent := SrcPage.Surface;
  B2.Caption := 'ZIP...';
  B2.Left := B1.Left + B1.Width + 8; B2.Top := B1.Top;
  B2.Width := ScaleX(110);
  B2.OnClick := @BrowseFile;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = UpdatePage.ID then
  begin
    UpdatePage.Caption := MaintenanceTitle(WizardDirValue);
    UpdateText.Caption := 'Selected folder: ' + WizardDirValue + #13#10#13#10;
    if IsRegisteredDir(WizardDirValue) and (OldVer <> '') then
      UpdateText.Caption := UpdateText.Caption + 'Installed version: ' + OldVer + #13#10;
    UpdateText.Caption := UpdateText.Caption + 'Setup version: ' + NewVer + #13#10#13#10;
    if GameFilesPresent(WizardDirValue) then
      UpdateText.Caption := UpdateText.Caption +
        'Setup found supported game files in this folder. It will reinstall the application, ' +
        'refresh the title-menu patch and keep the existing game content. Continue without a ZIP or source folder.'
    else
      UpdateText.Caption := UpdateText.Caption +
        'The game files in this folder are missing or incomplete. Select a supported game source on the next page.';
  end
  else if CurPageID = wpFinished then
  begin
    if InstallFailed then
      WizardForm.FinishedLabel.Caption := 'Setup encountered an error. Review the error message or Setup log, then run Setup again.'
    else if UpdateOnly then
      WizardForm.FinishedLabel.Caption := 'NBA Unrivaled ' + NewVer + ' is ready.' + #13#10#13#10 +
        'Setup reused the game files already in your installation folder.';
  end;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo,
  MemoGroupInfo, MemoTasksInfo: String): String;
begin
  if GameFilesPresent(WizardDirValue) then
    Result := MaintenanceTitle(WizardDirValue) + NewLine + Space + WizardDirValue + NewLine + NewLine +
      'Setup will reuse the existing game files and refresh the title-menu patch.'
  else
    Result := MemoDirInfo + NewLine + NewLine +
      'Game import source:' + NewLine + Space + Trim(SrcEdit.Text);
  if MemoTasksInfo <> '' then Result := Result + NewLine + NewLine + MemoTasksInfo;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if PageID = UpdatePage.ID then
    Result := not (IsRegisteredDir(WizardDirValue) or GameFilesPresent(WizardDirValue))
  else if PageID = SrcPage.ID then
    Result := GameFilesPresent(WizardDirValue);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = SrcPage.ID then
    if (Trim(SrcEdit.Text) = '') or
       not (FileExists(Trim(SrcEdit.Text)) or DirExists(Trim(SrcEdit.Text))) then
    begin
      SuppressibleMsgBox('Select an existing Xbox LIVE package, ZIP, or extracted game folder.', mbError, MB_OK, IDOK);
      Result := False;
    end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  UpdateOnly := GameFilesPresent(WizardDirValue);
  if UpdateOnly then
    Log('Using existing game files in ' + WizardDirValue + '\game')
  else if (Trim(SrcEdit.Text) = '') or
          not (FileExists(Trim(SrcEdit.Text)) or DirExists(Trim(SrcEdit.Text))) then
    Result := 'Select your Xbox LIVE package, ZIP, or extracted NBA Unrivaled game folder.';
end;

function VcRuntimePresent: Boolean;
begin
  { Check the VC++ DLLs imported by the executables and rexruntime.dll. }
  Result := FileExists(ExpandConstant('{sys}\vcruntime140.dll')) and
            FileExists(ExpandConstant('{sys}\vcruntime140_1.dll')) and
            FileExists(ExpandConstant('{sys}\msvcp140.dll')) and
            FileExists(ExpandConstant('{sys}\msvcp140_atomic_wait.dll'));
end;

function EnsureVcRedist: Boolean;
var
  Code: Integer;
  Params: String;
  ShowUi: Boolean;
  Launched: Boolean;
begin
  Result := True;
  if VcRuntimePresent then
    Exit;

  ShowUi := not WizardSilent;
  if ShowUi then
  begin
    if MsgBox(
         'Microsoft Visual C++ Redistributable (x64) is required.' + #13#10#13#10 +
         'This PC is missing one or more required Visual C++ runtime files. Without them, ' +
         'Setup cannot copy the game files (nba_import) and NBA Unrivaled / Settings will not start.' + #13#10#13#10 +
         'Windows may ask for administrator permission only for this redistributable step — ' +
         'the rest of Setup does not need admin.' + #13#10#13#10 +
         'Click Yes to install the redistributable now (included with Setup), or No to stop.',
         mbConfirmation, MB_YESNO) <> IDYES then
    begin
      Result := False;
      Exit;
    end;
  end;

  WizardForm.StatusLabel.Caption := 'Installing Visual C++ runtime (admin only for this step)...';
  WizardForm.FilenameLabel.Caption := '';
  { Interactive: /passive shows progress. Silent Setup: /quiet. }
  if ShowUi then
    Params := '/install /passive /norestart'
  else
    Params := '/install /quiet /norestart';

  { Elevate only the redist. Whole Setup stays PrivilegesRequired=lowest. }
  if IsAdmin then
    Launched := Exec(ExpandConstant('{tmp}\vc_redist.x64.exe'), Params,
        '', SW_SHOWNORMAL, ewWaitUntilTerminated, Code)
  else
    Launched := ShellExec('runas', ExpandConstant('{tmp}\vc_redist.x64.exe'), Params,
        '', SW_SHOWNORMAL, ewWaitUntilTerminated, Code);
  if not Launched then
    Code := 99;

  { 0 = ok, 1638 = already installed, 3010 = ok reboot needed }
  if (Code <> 0) and (Code <> 1638) and (Code <> 3010) then
  begin
    Log('Visual C++ runtime setup failed with exit code ' + IntToStr(Code));
    if ShowUi then
      MsgBox('Visual C++ runtime setup failed (code ' + IntToStr(Code) + ').' + #13#10#13#10 +
        'Approve the UAC prompt for the redistributable, or install the Microsoft Visual C++ ' +
        'Redistributable (x64) yourself, then run Setup again.',
        mbError, MB_OK);
    Result := False;
    Exit;
  end;

  if not VcRuntimePresent then
  begin
    Log('Required Visual C++ runtime DLLs are still missing.');
    if ShowUi then
      MsgBox('Required Visual C++ runtime files are still missing. Restart Windows and try Setup again.',
        mbError, MB_OK);
    Result := False;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Code: Integer;
  Msg: String;
begin
  if CurStep = ssPostInstall then
  begin
    { Always ensure CRT before import or leaving a repaired game. }
    if not EnsureVcRedist then
    begin
      InstallFailed := True;
      if not UpdateOnly then
        SuppressibleMsgBox('Visual C++ runtime is required to finish installing the game.',
          mbError, MB_OK, IDOK);
      Exit;
    end;
    if not UpdateOnly then
    begin
      WizardForm.StatusLabel.Caption := 'Copying the game files from your LIVE package or folder...';
      WizardForm.FilenameLabel.Caption := '';
      if not Exec(ExpandConstant('{app}\nba_import.exe'),
          '--prepare ' + AddQuotes(Trim(SrcEdit.Text)) + ' ' + AddQuotes(ExpandConstant('{app}\game')),
          '', SW_HIDE, ewWaitUntilTerminated, Code) then
        Code := 99;
      if Code <> 0 then
      begin
        case Code of
          2: Msg := 'The ZIP or folder could not be found.';
          3: Msg := 'That is not a usable NBA Unrivaled source. Prefer the Xbox LIVE package tree (584109D4\000D0000\<content>); an extracted folder/ZIP with default.xex also works.';
          4: Msg := 'Setup could not verify the inner Xbox LIVE content file or default.xex. Check that your ZIP or folder contains a supported NBA Unrivaled build.';
          5: Msg := 'Some game files are missing or the wrong size. The dump or STFS package may be incomplete.';
          6: Msg := 'The game files could not be written to the install folder.';
        else
          Msg := 'The game files could not be copied.';
        end;
        InstallFailed := True;
        Log('Game import failed with exit code ' + IntToStr(Code) + ': ' + Msg);
        SuppressibleMsgBox(Msg + #13#10#13#10 +
          'Run Setup again and select a correct LIVE package (title 584109D4) or extracted folder (default.xex).', mbError, MB_OK, IDOK);
        Exit;
      end;
    end;
    { Fresh import and update/repair: patch the imported FE_MAINMENU*.XUR so LIVE
      hard-block + Quit Game land even when the ZIP page was skipped. }
    if GameFilesPresent(ExpandConstant('{app}')) then
    begin
      WizardForm.StatusLabel.Caption := 'Patching title menu (LIVE hard-block)...';
      WizardForm.FilenameLabel.Caption := '';
      if not Exec('powershell.exe',
        '-NoProfile -ExecutionPolicy Bypass -File ' +
        AddQuotes(ExpandConstant('{app}\launcher-support\Apply-TitleMenuPatches.ps1')) +
        ' -GameRoot ' + AddQuotes(ExpandConstant('{app}\game')),
        '', SW_HIDE, ewWaitUntilTerminated, Code) then
        Code := 99;
      if Code <> 0 then
      begin
        InstallFailed := True;
        Log('Title-menu patch failed with exit code ' + IntToStr(Code));
        SuppressibleMsgBox('Setup could not apply the title-menu patch. See the Setup log for details.',
          mbError, MB_OK, IDOK);
      end;
    end;
  end;
end;

function GetCustomSetupExitCode: Integer;
begin
  Result := 0;
  if InstallFailed or not GameFilesPresent(ExpandConstant('{app}')) then
    Result := 1;
end;
