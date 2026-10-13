@echo off
rem 从任意工作目录启动游戏；首次启动时自动构建。
if not exist "%~dp0build\MX002.exe" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
    if errorlevel 1 (
        pause
        exit /b 1
    )
)
"%~dp0build\MX002.exe"
if errorlevel 1 pause
