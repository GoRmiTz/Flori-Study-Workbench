@echo off
title Flori Server
cd /d "%~dp0"

echo ============================================
echo   Flori Server Launcher  (芙洛理服务端启动器)
echo ============================================
echo.

where node >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Node.js not found on this PC.
    echo   Please install LTS from https://nodejs.org , then re-open this.
    echo.
    pause
    exit /b 1
)

echo Starting server ... keep this window OPEN.
echo   (To stop: press Ctrl+C, or just close this window)
echo.
node server.js
echo.
echo [Server stopped] Press any key to close this window.
pause
