; WowType 的 Windows 安装程序（Inno Setup 6）。双击 → 系统问一次管理员权限 → 装到 Program Files、注册输入法、
; 开始菜单放「WowType 设置」；「设置 → 应用」里能卸载。升级直接再装一次新版。
;
; windows/package.sh 先拼好安装包目录（wowtype.dll、wowtype-settings.exe、data\），再用 ISCC 编（CI 的 Windows 任务）：
;   ISCC /DAppVersion=0.6.0 /DArch=x64 /DSourceDir=<安装包目录> /DOutputDir=<输出目录> windows\installer\wowtype.iss
; 产物：WowType-<版本>-windows-<架构>-setup.exe。没有代码签名：下载后第一次运行 SmartScreen 会拦一下（docs/WINDOWS.md）。
;
; 这个文件要存成带 BOM 的 UTF-8（Inno Setup 靠 BOM 认中文）。

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef Arch
  #define Arch "x64"
#endif
#ifndef SourceDir
  #error 要用 /DSourceDir=<windows/package.sh 打的安装包目录> 编
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
; 别改：系统按它认出这是同一个程序（升级、卸载）
AppId={{8AF92801-E722-42A0-A358-07EFD5436DF4}
AppName=WowType
AppVersion={#AppVersion}
AppVerName=WowType {#AppVersion}
AppPublisher=WowType
AppPublisherURL=https://github.com/Gloridust/WowType
AppSupportURL=https://github.com/Gloridust/WowType/issues
AppUpdatesURL=https://github.com/Gloridust/WowType/releases
VersionInfoVersion={#AppVersion}
; 商店应用、开始菜单的搜索框只能载入 Program Files 这类位置的输入法：不让选目录
DefaultDirName={autopf}\WowType
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableWelcomePage=yes
DisableReadyPage=yes
PrivilegesRequired=admin
#if Arch == "arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif
; Windows 10 1803：系统自带 curl.exe（互译）、tar.exe（设置里下载语音模型）
MinVersion=10.0.17134
OutputDir={#OutputDir}
OutputBaseFilename=WowType-{#AppVersion}-windows-{#Arch}-setup
SetupIconFile=..\res\wowtype.ico
UninstallDisplayName=WowType 输入法
UninstallDisplayIcon={app}\wowtype-settings.exe
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
; 输入法装在每个应用的进程里：不要去关这些应用（换 DLL 的办法见下面的 [Code]）
CloseApplications=no
RestartApplications=no
ShowLanguageDialog=no

[Languages]
; Inno Setup 没带官方的简体中文，下面 [Messages] 把这个安装程序用得到的几句换成中文
Name: "zh"; MessagesFile: "compiler:Default.isl"

[Messages]
SetupAppTitle=安装 WowType
SetupWindowTitle=安装 %1
UninstallAppTitle=卸载 WowType
UninstallAppFullTitle=卸载 %1
ButtonBack=< 上一步(&B)
ButtonNext=下一步(&N) >
ButtonInstall=安装(&I)
ButtonOK=确定
ButtonCancel=取消
ButtonYes=是(&Y)
ButtonYesToAll=全是(&A)
ButtonNo=否(&N)
ButtonNoToAll=全否(&O)
ButtonFinish=完成(&F)
ButtonBrowse=浏览(&B)…
ExitSetupTitle=退出安装
ExitSetupMessage=还没装完。现在退出就不装了。%n%n以后可以再运行这个安装程序。%n%n退出吗？
AdminPrivilegesRequired=要用管理员身份安装（注册输入法要写系统设置）。
WizardInstalling=正在安装
InstallingLabel=正在把 [name] 装到这台电脑上，请稍等。
FinishedHeadingLabel=装好了
FinishedLabelNoIcons=[name] 装好了。%n%n按 Win + 空格 切到 WowType 就能打字；任务栏上的「中」按钮右键能打开设置。
FinishedLabel=[name] 装好了。%n%n按 Win + 空格 切到 WowType 就能打字；开始菜单里的「WowType 设置」能改设置。正在用着的应用重新打开以后用新装的版本。
ClickFinish=点「完成」关掉安装程序。
FinishedRestartLabel=要重启电脑才能装完。现在重启吗？
FinishedRestartMessage=要重启电脑才能装完。%n%n现在重启吗？
YesRadio=现在重启(&Y)
NoRadio=以后再说(&N)
StatusClosingApplications=正在关闭应用……
StatusCreateDirs=正在建目录……
StatusExtractFiles=正在复制文件……
StatusCreateIcons=正在建快捷方式……
StatusRegisterFiles=正在注册输入法……
StatusSavingUninstall=正在记下卸载信息……
StatusRunProgram=快装好了……
StatusRollback=正在撤销已经做了的……
ConfirmUninstall=要卸载 %1 吗？学到的词和设置（在你自己的数据目录里）会留着。
OnlyAdminCanUninstall=要用管理员身份卸载。
UninstallStatusLabel=正在从这台电脑上卸载 %1，请稍等。
UninstalledAll=%1 卸载好了。
UninstalledMost=%1 卸载好了。%n%n有几个文件正被应用用着，重启电脑以后会删掉。
UninstalledAndNeedsRestart=要重启电脑才能卸载干净。%n%n现在重启吗？
WizardUninstalling=正在卸载
StatusUninstalling=正在卸载 %1……
WindowsVersionNotSupported=WowType 要 Windows 10 1803 或更新的版本。
OnlyOnTheseArchitectures=这个安装程序只能装在这些处理器的 Windows 上：%n%n%1

[InstallDelete]
; 以前升级时换下来的旧 DLL：现在没人用了就删掉（还被用着的删不掉，留到下一次）
Type: files; Name: "{app}\wowtype.dll.old-*"

[Files]
; regserver：装好后调 DllRegisterServer（COM 类、简体中文的输入法配置、能力类别），卸载时先注销
Source: "{#SourceDir}\wowtype.dll"; DestDir: "{app}"; Flags: ignoreversion regserver uninsrestartdelete
Source: "{#SourceDir}\wowtype-settings.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\README.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{autoprograms}\WowType 设置"; Filename: "{app}\wowtype-settings.exe"

[Run]
Filename: "{app}\wowtype-settings.exe"; Description: "打开 WowType 设置"; Flags: postinstall nowait skipifsilent unchecked

[UninstallDelete]
Type: files; Name: "{app}\wowtype.dll.old-*"

[Code]
// 正在用着输入法的应用锁着旧的 wowtype.dll：不能覆盖，但能改名。改了名，新的照样装上，
// 这些应用重开以后用新的（和 install.ps1 一样）。
procedure CurStepChanged(CurStep: TSetupStep);
var
  Dll, Old: String;
begin
  if CurStep = ssInstall then
  begin
    Dll := ExpandConstant('{app}\wowtype.dll');
    if FileExists(Dll) then
    begin
      Old := Dll + '.old-' + GetDateTimeString('yyyymmddhhnnss', #0, #0);
      if not RenameFile(Dll, Old) then
        Log('换不下旧的 wowtype.dll：' + Dll);
    end;
  end;
end;
