@echo off
chcp 65001 >nul
rem 从任意工作目录构建并启动，确保拉取更新后不会运行旧程序。
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
if errorlevel 1 (
    pause
    exit /b 1
)
rem 将可选命令行参数传给游戏，以便通过同一启动入口执行验证。
"%~dp0build\MX002.exe" %*
if errorlevel 1 (
    pause
    exit /b 1
)
exit /b 0
