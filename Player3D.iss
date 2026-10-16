; =====================================================
; Player3D 安装包打包脚本 (Inno Setup)
; 使用方法: 用 Inno Setup Compiler 打开此文件，点击编译
; =====================================================

#define AppName "ReplayScope"
; 版本号由 deploy.bat 从根目录 VERSION 文件读出后通过 ISCC /DAppVersion=x.y.z 注入。
; 单独用 Inno Setup Compiler 打开本文件时，下面的默认值兜底。
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#define AppPublisher "DeepRobotics"
; 安装包文件名用的英文产品名（中文文件名在浏览器下载、邮件附件、命令行里都容易出问题）
; 对外产品名统一使用 ReplayScope
#define AppSlug "ReplayScope"
#define AppURL "https://github.com/zcdeepai-stack/ReplayScope"
#define AppExeName "Player3D.exe"
#define AppShortName "ReplayScope"

; 以本脚本所在目录为项目根目录，克隆到任意路径都可以打包。
#define DeployDir SourcePath + "\deploy"
#define IconFile SourcePath + "\x30PybusSimlu\image\icon.ico"

[Setup]
AppId={{B5F3A901-Player3D-2026}}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/support
AppUpdatesURL={#AppURL}/update
DefaultDirName={autopf}\{#AppSlug}
DisableDirPage=no
DefaultGroupName={#AppName}
AllowNoIcons=yes
OutputDir={#DeployDir}\..\installer_output
; 商业软件通行命名：<产品>-<版本>-<架构>-Setup，纯 ASCII，便于分发与下载
OutputBaseFilename={#AppSlug}-{#AppVersion}-x64-Setup
; 用 max 而非 ultra64：ultra64 的字典是 64MB，压缩这 1.5GB 内容需要 700MB 以上的
; 连续内存，机器上开着 IDE/浏览器时会直接 "Out of memory" 打包失败。
; max 内存占用约为其 1/4，速度快一倍多，体积只大 3~5%，更适合日常出包。
Compression=lzma2/max
SolidCompression=yes
; 限制压缩线程数，避免多线程各自申请大块内存导致峰值过高
LZMANumBlockThreads=2
SetupIconFile={#IconFile}
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName} {#AppVersion}
UninstallFilesDir={app}\_uninstall_cache
ArchitecturesInstallIn64BitMode=x64
VersionInfoVersion={#AppVersion}.0
VersionInfoDescription={#AppName} 安装程序
VersionInfoCompany={#AppPublisher}
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}
VersionInfoCopyright=Copyright (C) 2026 {#AppPublisher}
; 只允许 64 位系统安装，避免在 32 位机器上装完跑不起来
ArchitecturesAllowed=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
WizardStyle=modern
SetupLogging=yes

[Languages]
Name: "zh"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[CustomMessages]
zh.UpgradePrompt=检测到旧版本 (%1)，是否升级到新版本 (%2)？
zh.SameOrLowerPrompt=检测到已安装版本 (%1)，当前安装版本 (%2)。是否覆盖安装？
zh.UninstallFailed=卸载失败 (代码: %1)。请关闭所有相关程序后重试。

[Tasks]
Name: "desktopicon"; Description: "在桌面创建快捷方式"; GroupDescription: "附加任务："

[Files]
; === Qt 主程序 + windeployqt 产物 + SSH Helper ===
Source: "{#DeployDir}\*"; DestDir: "{app}"; \
    Excludes: "x30PybusSimlu,M20pybusSimlu,DR02pybusSimlu,roboscope-web"; \
    Flags: recursesubdirs createallsubdirs

; === X30 PyBullet 服务 (PyInstaller 产物 + URDF) ===
Source: "{#DeployDir}\x30PybusSimlu\*"; DestDir: "{app}\x30PybusSimlu"; \
    Flags: recursesubdirs createallsubdirs

; === M20 PyBullet 服务 (PyInstaller 产物 + URDF) ===
Source: "{#DeployDir}\M20pybusSimlu\*"; DestDir: "{app}\M20pybusSimlu"; \
    Flags: recursesubdirs createallsubdirs

; === DR02 人形 PyBullet 独立服务与模型 ===
Source: "{#DeployDir}\DR02pybusSimlu\*"; DestDir: "{app}\DR02pybusSimlu"; \
    Flags: recursesubdirs createallsubdirs

; === Web 服务 (Go 后端 + React 前端) ===
Source: "{#DeployDir}\roboscope-web\*"; DestDir: "{app}\roboscope-web"; \
    Flags: recursesubdirs createallsubdirs

[Registry]
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\Player3D_is1"; \
    ValueType: string; ValueName: "DisplayName"; ValueData: "{#AppName}"
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\Player3D_is1"; \
    ValueType: string; ValueName: "DisplayVersion"; ValueData: "{#AppVersion}"
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\Player3D_is1"; \
    ValueType: string; ValueName: "InstallLocation"; ValueData: "{app}"
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\Player3D_is1"; \
    ValueType: string; ValueName: "UninstallString"; ValueData: """{uninstallexe}"""

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"; IconFilename: "{app}\{#AppExeName}"
Name: "{group}\卸载 {#AppName}"; Filename: "{uninstallexe}"
Name: "{commondesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; IconFilename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "运行 {#AppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
Type: filesandordirs; Name: "{userappdata}\Player3D"
Type: filesandordirs; Name: "{localappdata}\Player3D"
Type: files; Name: "{group}\{#AppName}.lnk"
Type: files; Name: "{commondesktop}\{#AppName}.lnk"

[Code]
function CompareVersion(V1, V2: string): Integer;
var P1, P2, N1, N2: Integer;
begin
  Result := 0;
  while (Result = 0) and ((V1 <> '') or (V2 <> '')) do
  begin
    P1 := Pos('.', V1); P2 := Pos('.', V2);
    if P1 > 0 then begin N1 := StrToIntDef(Copy(V1, 1, P1 - 1), 0); V1 := Copy(V1, P1 + 1, MaxInt); end
    else begin N1 := StrToIntDef(V1, 0); V1 := ''; end;
    if P2 > 0 then begin N2 := StrToIntDef(Copy(V2, 1, P2 - 1), 0); V2 := Copy(V2, P2 + 1, MaxInt); end
    else begin N2 := StrToIntDef(V2, 0); V2 := ''; end;
    Result := N1 - N2;
  end;
end;

function InitializeSetup(): Boolean;
var
  InstalledVersion, UninstallCmd: string;
  ResultCode, RetryCount: Integer;
  PromptMsg: string;
begin
  Result := True;
  if RegQueryStringValue(HKLM, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\Player3D_is1', 'UninstallString', UninstallCmd) then
  begin
    RegQueryStringValue(HKLM, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\Player3D_is1', 'DisplayVersion', InstalledVersion);
    if InstalledVersion = '' then InstalledVersion := '1.0.0';

    if CompareVersion(InstalledVersion, '{#AppVersion}') >= 0 then
      PromptMsg := FmtMessage(CustomMessage('SameOrLowerPrompt'), [InstalledVersion, '{#AppVersion}'])
    else
      PromptMsg := FmtMessage(CustomMessage('UpgradePrompt'), [InstalledVersion, '{#AppVersion}']);

    if MsgBox(PromptMsg, mbConfirmation, MB_YESNO) = IDYES then
    begin
      if Pos('"', UninstallCmd) = 1 then
        UninstallCmd := Copy(UninstallCmd, 2, Length(UninstallCmd) - 2);

      RetryCount := 0;
      while RetryCount < 3 do
      begin
        if ShellExec('', UninstallCmd, '/SILENT /NORESTART', '', SW_SHOW, ewWaitUntilTerminated, ResultCode) then
        begin
          if ResultCode = 0 then
          begin
            Sleep(3000);
            Break;
          end
          else if RetryCount = 2 then
          begin
            MsgBox(FmtMessage(CustomMessage('UninstallFailed'), [IntToStr(ResultCode)]), mbError, MB_OK);
            Result := False;
            Exit;
          end;
        end;
        Inc(RetryCount);
        Sleep(1000);
      end;
    end
    else
      Result := False;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    ForceDirectories(ExpandConstant('{app}\wave_configs'));
  end;
end;

function InitializeUninstall(): Boolean;
begin
  MsgBox('即将卸载 {#AppName} 及所有配置数据，请确保程序未运行。', mbInformation, MB_OK);
  Result := True;
end;
