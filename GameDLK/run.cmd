@echo off
rem 从任意工作目录构建并启动，确保拉取更新后不会运行旧程序。
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
if errorlevel 1 (
    pause
    exit /b 1
)
"%~dp0build\MX002.exe"
if errorlevel 1 pause
