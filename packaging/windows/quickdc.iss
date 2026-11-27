; The QuickDC installer, built by packaging/windows/build.sh.
;
;   iscc /DAppVersion=0.1.0 /DNumericVersion=0.1.0.0 /DStage=<deployed tree> /DOutputDir=<dir> quickdc.iss
;
; Per user by default, with no elevation: the updater runs this with /VERYSILENT on
; quit, and a prompt for administrator rights is something nobody is there to answer.
; Installing for every user is offered, and asks for the rights it needs.

#ifndef AppVersion
  #error AppVersion must be given: /DAppVersion=...
#endif
#ifndef NumericVersion
  #define NumericVersion "0.0.0.0"
#endif
#ifndef Stage
  #error Stage must be given: /DStage=<the deployed tree>
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
; Fixed for good: this is how Windows knows an upgrade from a second installation.
AppId={{64AAB7EB-F31D-48CA-977B-3664C8C6943C}
AppName=QuickDC
AppVersion={#AppVersion}
AppVerName=QuickDC {#AppVersion}
AppPublisher=QuickDC
AppPublisherURL=https://quickdc.org
VersionInfoVersion={#NumericVersion}
DefaultDirName={autopf}\QuickDC
DefaultGroupName=QuickDC
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10 1809, the oldest Qt 6.5 runs on.
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=QuickDC-{#AppVersion}-x86_64-setup
SetupIconFile=app.ico
UninstallDisplayIcon={app}\QuickDC.exe
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Closes a running QuickDC before replacing it, and starts it again after a silent update.
CloseApplications=yes
RestartApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; What an older version shipped and this one does not, so a plugin left behind is never loaded.
Type: files; Name: "{app}\*.dll"
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\styles"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\iconengines"
Type: filesandordirs; Name: "{app}\tls"
Type: filesandordirs; Name: "{app}\networkinformation"
Type: filesandordirs; Name: "{app}\generic"
Type: filesandordirs; Name: "{app}\translations"

[Icons]
Name: "{autoprograms}\QuickDC"; Filename: "{app}\QuickDC.exe"
Name: "{autodesktop}\QuickDC"; Filename: "{app}\QuickDC.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\QuickDC.exe"; Description: "{cm:LaunchProgram,QuickDC}"; Flags: nowait postinstall skipifsilent
