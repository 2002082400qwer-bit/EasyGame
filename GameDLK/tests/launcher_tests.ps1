# 从传统中文代码页和异工作目录运行真实启动器，覆盖双击入口的构建及游戏启动路径；无参数。
$ErrorActionPreference = 'Stop' # 发生脚本错误立即终止
$projectRoot = Split-Path -Parent $PSScriptRoot # 工程根目录
$launcher = Join-Path $projectRoot 'run.cmd' # 与用户双击相同的批处理入口
$startInfo = New-Object Diagnostics.ProcessStartInfo # 独立cmd进程的启动配置
$startInfo.FileName = $env:ComSpec
$startInfo.Arguments = '/d /c chcp 936 >nul & call "' + $launcher + '" --verify'
$startInfo.WorkingDirectory = $env:TEMP
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$startInfo.RedirectStandardInput = $true
$process = New-Object Diagnostics.Process # 本测试独占的启动器进程
$process.StartInfo = $startInfo
$startedAt = [DateTime]::UtcNow # 确保GPU报告确实由这次启动重新生成
[void]$process.Start()
$outputTask = $process.StandardOutput.ReadToEndAsync() # 异步读取标准输出，避免缓冲区阻塞
$errorTask = $process.StandardError.ReadToEndAsync() # 异步读取报错内容
$process.StandardInput.Close()
if (!$process.WaitForExit(120000)) {
    & taskkill.exe /PID $process.Id /T /F | Out-Null
    throw '启动器未在120秒内完成构建和GPU验证。'
}
$output = $outputTask.Result # 启动器及游戏的实际输出
$errors = $errorTask.Result # 启动器及游戏的实际错误
if ($process.ExitCode -ne 0 -or !$output.Contains('GPU scene RIGHT-crouch-dash-standing: PASS')) {
    Write-Output $output
    Write-Output $errors
    throw ('双击入口没有成功启动游戏验证，退出码：' + $process.ExitCode)
}
$reportPath = Join-Path $projectRoot 'verification\report.json' # 必须由该启动器新生成的GPU报告
$report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json # 当前报告内容
if ((Get-Item -LiteralPath $reportPath).LastWriteTimeUtc -lt $startedAt -or !$report.passed -or $report.checks -ne 95) {
    throw '启动器没有生成本次95项通过的GPU报告。'
}
Write-Output '启动器验证通过：中文代码页936、异工作目录、真实构建与95项GPU场景。'
