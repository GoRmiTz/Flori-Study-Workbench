@echo off
setlocal EnableDelayedExpansion
REM ============================================================
REM  芙洛理 Flori · 最小 CI（P0-3）
REM  一条命令串起三类测试：
REM     ① 构建（Flori + ws_contract_test）
REM     ② 服务端冒烟 smoke.js（51 项，覆盖认证/六块同步/专栏/WS 自习室）
REM     ③ 客户端契约 ws_contract_test（复用生产网络层连运行中的 Server）
REM     ④ 截图自检 17 路由 + 视觉基线比对（有桌面 GUI 才跑，无头环境自动跳过）
REM  退出码：0 = 全部通过；1 = 测试失败；2 = 网络/前置未就绪
REM  用法：在「Developer Command Prompt / VS 开发人员命令提示」里
REM        cd DesktopApp && tools\ci.bat
REM ============================================================
cd /d "%~dp0\.."
set "ROOT=%CD%"
set "SERVER_DIR=%ROOT%\..\Server"
set "BASE=http://127.0.0.1:8787"
set "EXITC=0"

REM ---- 0. 按需初始化 MSVC 环境 ----
if not defined VCINSTALLDIR (
  if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
  ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
  ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
  )
)

REM ---- 1. 构建 ----
echo [ci] 1/5 构建 Flori + ws_contract_test
cmake --build build --target Flori ws_contract_test
if errorlevel 1 (
  echo [ci] 构建失败
  exit /b 2
)

REM ---- 2. 启动 Server（后台） ----
echo [ci] 2/5 启动 Server（%BASE%）
if not exist "%SERVER_DIR%\node_modules" (
  echo [ci] 警告：Server\node_modules 缺失，请先在 Server\ 下 npm install
)
start "FloriServer" /min cmd /c "cd /d %SERVER_DIR% && node server.js"
set "READY=0"
for /L %%i in (1,1,30) do (
  powershell -NoProfile -Command "Test-NetConnection -ComputerName 127.0.0.1 -Port 8787 -InformationLevel Quiet" | findstr /i "True" >nul && (set "READY=1" & goto :served)
  timeout /t 1 >nul
)
:served
if "%READY%"=="0" (
  echo [ci] Server 未能在 30s 内就绪（网络/前置未就绪）
  exit /b 2
)

REM ---- 3. smoke.js ----
echo [ci] 3/5 运行 smoke.js
node "%SERVER_DIR%\tools\smoke.js" %BASE%
if errorlevel 1 (
  echo [ci] smoke.js 未全绿（退出 %errorlevel%）
  set "EXITC=1"
)

REM ---- 4. ws_contract_test ----
echo [ci] 4/5 运行 ws_contract_test
if exist "build\ws_contract_test.exe" (
  build\ws_contract_test.exe
  if errorlevel 1 (
    echo [ci] ws_contract_test 存在未联通项（退出 %errorlevel%）
    if not "%EXITC%"=="1" set "EXITC=2"
  )
) else (
  echo [ci] ws_contract_test.exe 未构建，跳过
  if not "%EXITC%"=="1" set "EXITC=2"
)

REM ---- 5. 截图自检（17 路由）+ 视觉基线比对 ----
REM  为什么分两层：--shot 只能证明「这一页没崩」；真正会伤用户的是
REM  「没崩但布局塌了」（文字压出卡片、按钮重叠）。所以再叠一层像素比对。
REM  无桌面 GUI 会话（服务/SSH/无头 CI）时整段跳过，不计入失败。
echo [ci] 5/5 截图自检（17 路由）+ 视觉基线比对
set "SHOTDIR=build\shots\ci"
if exist "%SHOTDIR%" rd /s /q "%SHOTDIR%" >nul 2>&1
mkdir "%SHOTDIR%" >nul 2>&1

REM 先用 home 探路：无桌面会话时这一发就会失败，整段直接跳过。
REM --at=1.5 等转场（0.62s）+ 各元素入场动画落稳，避免截到空帧。
build\Flori.exe --shot "%SHOTDIR%\home.png" --route home --at 1.5 >nul 2>&1
if errorlevel 1 (
  echo [ci] 无桌面 GUI 会话，截图自检跳过（不计失败）
  goto :shotdone
)

set "SHOTBAD=0"
for %%r in (loader cover home checkin room dash roadmap materials media video profile manage advisor achieve plan friend login) do (
  build\Flori.exe --shot "%SHOTDIR%\%%r.png" --route %%r --at 1.5 >nul 2>&1
  if errorlevel 1 (
    echo   FAIL %%r
    set /a SHOTBAD+=1
  ) else (
    echo   ok   %%r
  )
)
if not "!SHOTBAD!"=="0" (
  echo [ci] 截图自检 !SHOTBAD! 条路由未通过
  set "EXITC=1"
)

powershell -NoProfile -ExecutionPolicy Bypass -File "tools\shotdiff.ps1" -Mode check
if errorlevel 2 (
  echo [ci] 视觉基线未建立或无截图，跳过比对（首次执行 tools\shotdiff.ps1 -Mode baseline 固化）
) else if errorlevel 1 (
  echo [ci] 视觉基线比对发现回归
  set "EXITC=1"
)
:shotdone

echo [ci] 完成，退出码=%EXITC%
exit /b %EXITC%
