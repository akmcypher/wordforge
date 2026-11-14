; Inno Setup 6.3+ script for the Wordforge Windows installer. It installs the folder that
; tools/package-windows.py staged (the same files as the portable ZIP), per user by default so
; no administrator prompt appears, adds Start menu and optional desktop shortcuts, makes
; Wordforge the handler for .wfg and offers it under "Open with" for .docx and .txt.
;
; ISCC /DAppVersion=0.6.1 /DArch=x64 /DSourceDir=out\Wordforge-0.6.1-windows-x64 /Oout wordforge.iss
; Arch is x64 or arm64. Output: Wordforge-<version>-windows-<arch>-setup.exe

#ifndef AppVersion
  #error Pass /DAppVersion=<version>
#endif
#ifndef Arch
  #error Pass /DArch=x64 or /DArch=arm64
#endif
#ifndef SourceDir
  #error Pass /DSourceDir=<staged package folder>
#endif
#if Arch == "x64"
  #define AllowedArch "x64compatible"
#elif Arch == "arm64"
  #define AllowedArch "arm64"
#else
  #error Arch must be x64 or arm64
#endif

[Setup]
AppId={{6F3C2B1E-8A4D-4C57-9E2B-5D7A1F0C9B34}
AppName=Wordforge
AppVersion={#AppVersion}
AppVerName=Wordforge {#AppVersion}
AppPublisher=Wordforge contributors
AppPublisherURL=https://github.com/akmcypher/wordforge
AppSupportURL=https://github.com/akmcypher/wordforge/issues
AppUpdatesURL=https://github.com/akmcypher/wordforge/releases
VersionInfoVersion={#AppVersion}
VersionInfoProductName=Wordforge
VersionInfoDescription=Wordforge {#AppVersion} setup
DefaultDirName={autopf}\Wordforge
DefaultGroupName=Wordforge
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed={#AllowedArch}
ArchitecturesInstallIn64BitMode={#AllowedArch}
MinVersion=10.0.18362
ChangesAssociations=yes
CloseApplications=yes
UninstallDisplayIcon={app}\Wordforge.exe
UninstallDisplayName=Wordforge
SetupIconFile=wordforge.ico
WizardStyle=modern
Compression=lzma2/ultra64
SolidCompression=yes
OutputBaseFilename=Wordforge-{#AppVersion}-windows-{#Arch}-setup

[Tasks]
Name: desktopicon; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Wordforge"; Filename: "{app}\Wordforge.exe"
Name: "{autodesktop}\Wordforge"; Filename: "{app}\Wordforge.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\Wordforge.Document"; ValueType: string; ValueName: ""; ValueData: "Wordforge document"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Wordforge.Document\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Wordforge.exe,0"
Root: HKA; Subkey: "Software\Classes\Wordforge.Document\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Wordforge.exe"" ""%1"""
Root: HKA; Subkey: "Software\Classes\.wfg"; ValueType: string; ValueName: ""; ValueData: "Wordforge.Document"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.wfg\OpenWithProgids"; ValueType: string; ValueName: "Wordforge.Document"; ValueData: ""; Flags: uninsdeletevalue
; "Open with" only: never take .docx or .txt away from the user's current program.
Root: HKA; Subkey: "Software\Classes\Applications\Wordforge.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Wordforge"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\Wordforge.exe\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Wordforge.exe"" ""%1"""
Root: HKA; Subkey: "Software\Classes\Applications\Wordforge.exe\SupportedTypes"; ValueType: string; ValueName: ".wfg"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\Wordforge.exe\SupportedTypes"; ValueType: string; ValueName: ".docx"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\Wordforge.exe\SupportedTypes"; ValueType: string; ValueName: ".txt"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\.docx\OpenWithList\Wordforge.exe"; ValueType: string; ValueName: ""; ValueData: ""; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\.txt\OpenWithList\Wordforge.exe"; ValueType: string; ValueName: ""; ValueData: ""; Flags: uninsdeletekey

[Run]
Filename: "{app}\Wordforge.exe"; Description: "{cm:LaunchProgram,Wordforge}"; Flags: nowait postinstall skipifsilent
