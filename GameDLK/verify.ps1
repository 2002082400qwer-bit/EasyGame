# 执行真实GPU、异工作目录启动与资源错误验证；先运行build.ps1生成程序，无参数。
$ErrorActionPreference = 'Stop' # 任何脚本错误立即停止
$projectRoot = $PSScriptRoot # 当前工程根目录
$executable = Join-Path $projectRoot 'build\MX002.exe' # 待验证的已构建游戏
$verificationRoot = Join-Path $projectRoot 'verification' # 所有验证产物的输出目录
if (!(Test-Path -LiteralPath $executable)) { throw '请先执行build.ps1。' }
Push-Location -LiteralPath $env:TEMP
try {
    & $executable --verify
    if ($LASTEXITCODE -ne 0) { throw '真实GPU验证失败。' }
} finally {
    Pop-Location
}
$report = Get-Content -Raw -LiteralPath (Join-Path $verificationRoot 'report.json') | ConvertFrom-Json # 正式GPU报告
if (!$report.passed -or $report.atlasCount -ne 35 -or $report.checks -ne 95 -or $report.scenes.Count -ne 95) {
    throw 'GPU报告数量或结论不符合预期。'
}
foreach ($scene in $report.scenes) { # 当前图集或动作验证记录
    if (!$scene.passed -or $scene.frame -lt 0 -or $scene.frame -gt 59) { throw ('场景断言失败：' + $scene.name) }
    if ($scene.name.StartsWith('atlas-') -and $scene.frame -ne 29) { throw '图集截图与报告必须使用相同帧号。' }
    if ($scene.clip.StartsWith('turn_') -and [Math]::Abs($scene.renderHeight - 208) -gt 0.01) { throw '转身人物高度没有保持站立尺度。' }
    $screenshot = Join-Path $verificationRoot ($scene.name + '.png') # 对应该记录的真实GPU截图
    if (!(Test-Path -LiteralPath $screenshot) -or (Get-Item -LiteralPath $screenshot).Length -lt 100) {
        throw ('截图缺失或为空：' + $scene.name)
    }
}
$probeRoot = Join-Path $verificationRoot ('resource-probe-' + [guid]::NewGuid().ToString('N')) # 隔离的资源错误测试目录，不改正式图片
$probeBuild = Join-Path $probeRoot 'build' # 保持与正式工程相同的程序相对位置
New-Item -ItemType Directory -Path $probeBuild -Force | Out-Null
$probeExecutable = Join-Path $probeBuild 'MX002.exe' # 仅用于错误分支的程序副本
Copy-Item -LiteralPath $executable -Destination $probeExecutable
$missingError = Join-Path $probeRoot 'missing.stderr.txt' # 缺失资源报错内容
$process = Start-Process -FilePath $probeExecutable -ArgumentList '--verify' -Wait -PassThru -WindowStyle Hidden -RedirectStandardError $missingError # 缺少全部资源的子进程
if ($process.ExitCode -ne 2 -or !(Get-Content -Raw -LiteralPath $missingError).Contains('front_idle.png')) {
    throw '缺少资源时未按预期退出并报告具体文件。'
}
$probeAtlases = Join-Path $probeRoot '图片\游戏素材\diluc-redrawn-60\atlases' # 只有PNG头部的损坏资源目录
New-Item -ItemType Directory -Path $probeAtlases -Force | Out-Null
foreach ($atlas in Get-ChildItem -LiteralPath (Join-Path $projectRoot '图片\游戏素材\diluc-redrawn-60\atlases') -Filter '*.png') { # 当前正式图集，仅复制头部以模拟截断文件
    $bytes = [IO.File]::ReadAllBytes($atlas.FullName) # 原图完整字节，不修改源文件
    [IO.File]::WriteAllBytes((Join-Path $probeAtlases $atlas.Name), [byte[]]$bytes[0..23])
}
$corruptError = Join-Path $probeRoot 'corrupt.stderr.txt' # 解码损坏资源时的报错内容
$process = Start-Process -FilePath $probeExecutable -ArgumentList '--verify' -Wait -PassThru -WindowStyle Hidden -RedirectStandardError $corruptError -RedirectStandardOutput (Join-Path $probeRoot 'corrupt.stdout.txt') # 头部正确但数据截断的子进程
if ($process.ExitCode -ne 2 -or !(Get-Content -Raw -LiteralPath $corruptError).Contains('side_idle.png')) {
    throw '损坏资源时未按预期退出并报告具体文件。'
}
Write-Output '验证通过：95个GPU场景、异工作目录启动、资源缺失及解码损坏两个错误分支。'
