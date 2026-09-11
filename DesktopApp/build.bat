@echo off
rem ============================================================
rem  芙洛理 Flori 桌面端 一键构建
rem  自动定位 Visual Studio BuildTools / CMake / Ninja
rem ============================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [X] 找不到 vswhere.exe，请先安装 Visual Studio Build Tools。
  exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if "%VSPATH%"=="" (
  echo [X] 未找到含 C++ 工具集的 Visual Studio 安装。
  exit /b 1
)
echo [i] VS: %VSPATH%

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
  echo [X] vcvars64.bat 调用失败。
  exit /b 1
)

set "CMAKE=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if not exist "%CMAKE%" set "CMAKE=cmake"
if not exist "%NINJA%" set "NINJA=ninja"

set "BUILDDIR=%~dp0build"
set "CFG=%1"
if "%CFG%"=="" set "CFG=RelWithDebInfo"

"%CMAKE%" -S "%~dp0." -B "%BUILDDIR%" -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
  -DCMAKE_BUILD_TYPE=%CFG%
if errorlevel 1 exit /b 1

"%CMAKE%" --build "%BUILDDIR%"
if errorlevel 1 exit /b 1

echo.
echo [OK] 构建完成: %BUILDDIR%\Flori.exe
endlocal
