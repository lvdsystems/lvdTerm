; Inno Setup script for lvdterm.
;

; Kept in sync by hand with project(lvdterm VERSION ...) in the root
; CMakeLists.txt - this file isn't part of the CMake build, so bump both.
#define AppVersion "0.3.1"

[Setup]
AppId={{6A9F6E63-6E7B-4B9A-9C0D-1B7E9B7F3E11}
AppName=lvdterm
AppVersion={#AppVersion}
AppPublisher=LVD Systems S.r.l.
DefaultDirName={autopf}\lvdterm
DefaultGroupName=lvdterm
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\lvdterm.exe
SetupIconFile=src\resources\icons\app_icon.ico
OutputDir=dist
OutputBaseFilename=lvdterm-setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
SourceDir=..
LicenseFile=LICENSE
;SignTool=lvdterm
;SignedUninstaller=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: desktopicon; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Files]
; Everything windeployqt placed next to the exe: the exe itself, the Qt
; runtime DLLs, and the plugin subfolders (platforms/, styles/, etc).
Source: "build\llvm-mingw-release\dist\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\lvdterm"; Filename: "{app}\lvdterm.exe"
Name: "{group}\Uninstall lvdterm"; Filename: "{uninstallexe}"
Name: "{autodesktop}\lvdterm"; Filename: "{app}\lvdterm.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\lvdterm.exe"; Description: "Launch lvdterm"; Flags: nowait postinstall skipifsilent
