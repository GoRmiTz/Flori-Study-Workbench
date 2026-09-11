; ============================================================
;  芙洛理 Flori 桌面端安装器（InnoSetup）
;  编译：用 InnoSetup 打开本文件，或命令行  iscc studyplan.iss
;  产物：..\out\installer\Flori-Setup-<版本>.exe
;
;  约定：
;   · 可执行文件位于仓库根 build\Flori.exe（Release 构建产物）
;   · 用户数据（accounts/ 等）首次运行生成于 %LOCALAPPDATA%\Flori，
;     不随安装包分发，卸载时由专用清理逻辑处理（见下方 [UninstallDelete]）
;   · 自动更新由客户端 Updater 完成（后台拉 Server /version），
;     本安装器只负责首次安装与卸载
; ============================================================
#define MyAppName    "芙洛理 Flori"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "芙洛理工作室"
#define MyAppURL     "https://example.com"
#define MyExe        "Flori.exe"

[Setup]
; 安装包唯一标识（换包时保持，便于升级/卸载识别）
AppId={{A1B2C3D4-0E11-4C7A-9B2F-6F3D5E7A1C90}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputDir=..\out\installer
OutputBaseFilename=Flori-Setup-{#MyAppVersion}
; SetupIconFile=..\assets\icon.ico        ; 可选：放置品牌 ico 后取消注释
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64
; 安装到 Program Files 需要管理员；也可改 nonadmin 放到用户目录（免 UAC）
PrivilegesRequired=admin
UninstallDisplayIcon={app}\{#MyExe}
; 允许静默卸载（自动更新不依赖安装器，仅说明）
Uninstallable=yes

[Languages]
Name: "chinese"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; 主程序（Release 构建：cmake --build build --config Release）
Source: "..\build\Flori.exe"; DestDir: "{app}"; Flags: ignoreversion
; 资源（着色器/图标等，如存在则取消注释并按需调整）
; Source: "..\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyExe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyExe}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "额外任务："

[Run]
Filename: "{app}\{#MyExe}"; Description: "启动 {#MyAppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 不强制删除用户数据；如需清理本地数据可取消注释（谨慎）
; Type: filesandordirs; Name: "{localappdata}\Flori"
