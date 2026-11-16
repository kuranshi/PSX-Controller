#ifndef AppVersion
#define AppVersion "0.1.0"
#endif

[Setup]
AppId={{DA8B90A0-3744-49F9-BA7B-B31A449D8839}
AppName=PSX Controller
AppVersion={#AppVersion}
AppPublisher=Kuranshi
DefaultDirName={autopf}\PSX Controller
DefaultGroupName=PSX Controller
OutputDir=..\dist\installer
OutputBaseFilename=PSX-Controller-Setup-{#AppVersion}-x64
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\PSXController.exe
SetupLogging=yes
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "..\dist\app\PSXController\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "prerequisites\ViGEmBus_1.22.0_x64_x86_arm64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "..\..\LICENSE"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "..\THIRD-PARTY-NOTICES.md"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "..\licenses\vgamepad-LICENSE"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "..\licenses\pyserial-LICENSE.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "..\licenses\pyinstaller-COPYING.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "..\licenses\ViGEmBus-LICENSE"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "..\虚拟手柄使用说明.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\PSX Controller"; Filename: "{app}\PSXController.exe"
Name: "{autodesktop}\PSX Controller"; Filename: "{app}\PSXController.exe"; Tasks: desktopicon
Name: "{group}\使用说明"; Filename: "{app}\虚拟手柄使用说明.md"

[Run]
Filename: "{tmp}\ViGEmBus_1.22.0_x64_x86_arm64.exe"; StatusMsg: "正在安装虚拟手柄驱动…"; Flags: waituntilterminated; Check: not IsViGEmBusInstalled

[Code]
function IsViGEmBusInstalled(): Boolean;
begin
  Result := RegKeyExists(HKEY_LOCAL_MACHINE, 'SYSTEM\CurrentControlSet\Services\ViGEmBus');
end;

// by codex
