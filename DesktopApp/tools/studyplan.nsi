; ============================================================
;  芙洛理 Flori 桌面端安装器（NSIS）
;  编译：用 NSIS 打开本文件，或 makensis studyplan.nsi
;  产物：..\out\installer\Flori-Setup-<版本>.exe
;
;  与 studyplan.iss 等价，给没有 InnoSetup 的环境兜底。
;  现代 UI（MUI2），安装到 Program Files，提供桌面快捷方式任务。
; ============================================================
!define APPNAME      "芙洛理 Flori"
!define APPVERSION   "1.0.0"
!define PUBLISHER    "芙洛理工作室"
!define URL          "https://example.com"
!define EXE          "Flori.exe"

!include "MUI2.nsh"
!include "x64.nsh"

Name "${APPNAME} ${APPVERSION}"
OutFile "..\out\installer\Flori-Setup-${APPVERSION}.exe"
InstallDir "$PROGRAMFILES64\${APPNAME}"
InstallDirRegKey HKLM "Software\${APPNAME}" "InstallDir"

RequestExecutionLevel admin          ; 安装到 Program Files 需管理员
ManifestDPIAware true

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

; ---------- 安装段 ----------
Section "Main" SecMain
  SetOutPath "$INSTDIR"
  File "..\build\${EXE}"
  ; File /r "..\assets"        ; 资源目录（如存在取消注释）

  ; 快捷方式
  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\${EXE}"
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${EXE}"

  ; 注册信息（供卸载/自动更新定位安装目录）
  WriteRegStr HKLM "Software\${APPNAME}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" \
              "DisplayName" "${APPNAME}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" \
              "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" \
              "DisplayVersion" "${APPVERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" \
              "Publisher" "${PUBLISHER}"

  ; 卸载程序
  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

; ---------- 卸载段 ----------
Section "Uninstall"
  Delete "$INSTDIR\${EXE}"
  ; RMDir /r "$INSTDIR\assets"
  Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
  Delete "$DESKTOP\${APPNAME}.lnk"
  RMDir "$SMPROGRAMS\${APPNAME}"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"

  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"
  DeleteRegKey HKLM "Software\${APPNAME}"
SectionEnd
