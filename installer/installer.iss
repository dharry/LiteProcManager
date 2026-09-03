#define MyAppName "LiteProcManager"
#define MyAppPublisher "LiteProcManager"
#define MyAppExeName "LiteProcManager.exe"
#define MyAppVersion Trim(FileRead(FileOpen(SourcePath + "..\VERSION")))

[Setup]
AppId={{B3B9B2F0-3E7E-4C2B-9B0E-6C7B7A6E9C11}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={userappdata}\Programs\{#MyAppName}
DisableDirPage=yes
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
DisableWelcomePage=no
UninstallDisplayName={#MyAppName} ({username}@{code:GetUserDomain})
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputBaseFilename=LiteProcManagerSetup
OutputDir=..\dist
Compression=lzma2/fast
SolidCompression=yes
WizardStyle=modern
WizardImageFile=images\wizard_large.bmp
WizardSmallImageFile=images\wizard_small.bmp
SetupIconFile=images\app.ico
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=
ArchitecturesAllowed=x64compatible
;ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "..\bin\Release\LiteProcManager.exe"; DestDir: "{app}"; Excludes: "*.pdb"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "{#MyAppExeName}"; Flags: uninsdeletevalue dontcreatekey

[Code]
function GetUserDomain(Param: string): string;
begin
  Result := GetEnv('USERDOMAIN');
  if Result = '' then
    Result := 'unknown';
end;
