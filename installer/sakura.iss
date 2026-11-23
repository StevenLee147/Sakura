; installer/sakura.iss — Sakura-樱 Windows x64 Official Installer (Inno Setup 6)

#ifndef MyAppVersion
  #define MyAppVersion "1.0.0"
#endif

#ifndef SourceStageDir
  #define SourceStageDir "..\build\dist\Sakura-" + MyAppVersion + "-windows-x64"
#endif

#ifndef OutputDirPath
  #define OutputDirPath "..\build\dist"
#endif

#ifndef OutputBaseName
  #define OutputBaseName "Sakura-v" + MyAppVersion + "-win64-setup"
#endif

#define MyAppName "Sakura-樱"
#define MyAppPublisher "Sakura Rhythm Project"
#define MyAppURL "https://github.com/StevenLee147/Sakura"
#define MyAppExeName "Sakura.exe"

[Setup]
AppId={{9A6E7E58-5F84-4D32-9C71-7C9023E01000}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\Sakura
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
LicenseFile=..\THIRD_PARTY_NOTICES.md
OutputDir={#OutputDirPath}
OutputBaseFilename={#OutputBaseName}
SetupIconFile=..\resources\images\sakura.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/fast
SolidCompression=yes
WizardStyle=modern
#if VER >= EncodeVer(6,3,0)
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#else
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
#endif
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ChangesAssociations=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "fileassoc"; Description: "Associate .sakura chart package files with {#MyAppName}"; GroupDescription: "File associations:"

[Files]
Source: "{#SourceStageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"
Name: "{group}\Player Guide"; Filename: "{app}\doc\PLAYER_GUIDE.md"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.sakura"; ValueType: string; ValueName: ""; ValueData: "SakuraChartPackage"; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\SakuraChartPackage"; ValueType: string; ValueName: ""; ValueData: "Sakura Chart Package"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\SakuraChartPackage\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\SakuraChartPackage\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: fileassoc

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  UserDataDir: string;
begin
  if (CurUninstallStep = usPostUninstall) and (not UninstallSilent) then
  begin
    UserDataDir := ExpandConstant('{userappdata}\Sakura\Sakura');
    if DirExists(UserDataDir) then
    begin
      if MsgBox('Keep your personal player save data (scores, settings, replays, and custom charts) in:' + #13#10 +
                UserDataDir + #13#10 + #13#10 +
                'Click YES to keep your records safely (Recommended), or NO to permanently delete personal save data.',
                mbConfirmation, MB_YESNO or MB_DEFBUTTON1) = IDNO then
      begin
        DelTree(UserDataDir, True, True, True);
      end;
    end;
  end;
end;