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
if (!$report.passed -or $report.renderer -ne 'live-skeleton' -or $report.resourceCount -ne 48 -or $report.logicalStateCount -ne 35 -or $report.checks -ne 95 -or $report.scenes.Count -ne 95) {
    throw 'GPU报告数量或结论不符合预期。'
}
foreach ($scene in $report.scenes) { # 当前图集或动作验证记录
    if (!$scene.passed -or $scene.frame -lt 0 -or $scene.frame -gt 59) { throw ('场景断言失败：' + $scene.name) }
    if ($scene.loadedParts -ne 48) { throw 'GPU场景没有使用完整48个固定拆件。' }
    if ($scene.name.StartsWith('pose-') -and $scene.frame -ne 29) { throw '姿态截图与报告必须使用相同阶段。' }
    if ([Math]::Abs($scene.standingReferenceHeight - 208) -gt 0.01) { throw '固定骨架站姿标尺发生变化。' }
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
if ($process.ExitCode -ne 2 -or !(Get-Content -Raw -LiteralPath $missingError).Contains('front/head.png')) {
    throw '缺少资源时未按预期退出并报告具体文件。'
}
$probeParts = Join-Path $probeRoot '图片\游戏素材\骨骼拆分方式\parts' # 隔离工程唯一的图片目录，只含48个固定拆件
New-Item -ItemType Directory -Path (Split-Path -Parent $probeParts) -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot '图片\游戏素材\骨骼拆分方式\parts') -Destination $probeParts -Recurse
if ((Get-ChildItem -LiteralPath $probeParts -Recurse -Filter '*.png').Count -ne 48) { throw '隔离资源不是恰好48张。' }
$onlyPartsError = Join-Path $probeRoot 'only-parts.stderr.txt' # 无任何旧图集时的运行错误输出
$process = Start-Process -FilePath $probeExecutable -ArgumentList '--verify' -Wait -PassThru -WindowStyle Hidden -RedirectStandardError $onlyPartsError -RedirectStandardOutput (Join-Path $probeRoot 'only-parts.stdout.txt') # 仅48拆件也必须完整通过95项实际GPU场景
if ($process.ExitCode -ne 0) { throw '只保留48拆件的隔离工程无法运行，仍可能依赖旧图集。' }
$partsReport = Get-Content -LiteralPath (Join-Path $probeRoot 'verification/report.json') -Raw | ConvertFrom-Json # 隔离工程实际生成的报告
if (!$partsReport.passed -or $partsReport.checks -ne 95 -or $partsReport.resourceCount -ne 48) { throw '隔离工程没有通过完整骨骼场景。' }
$damagedPart = Join-Path $probeParts 'front/head.png' # 仅修改隔离副本，不触碰正式美术
$bytes = [IO.File]::ReadAllBytes($damagedPart) # 保存隔离拆件原始字节以构造截断PNG
[IO.File]::WriteAllBytes($damagedPart, [byte[]]$bytes[0..23])
$corruptError = Join-Path $probeRoot 'corrupt.stderr.txt' # 解码损坏资源时的报错内容
$process = Start-Process -FilePath $probeExecutable -ArgumentList '--verify' -Wait -PassThru -WindowStyle Hidden -RedirectStandardError $corruptError -RedirectStandardOutput (Join-Path $probeRoot 'corrupt.stdout.txt') # 头部正确但数据截断的子进程
if ($process.ExitCode -ne 2 -or !(Get-Content -Raw -LiteralPath $corruptError).Contains('front/head.png')) {
    throw '损坏资源时未按预期退出并报告具体文件。'
}
$summary = [ordered]@{ renderer='live-skeleton'; passed=$true; gpuScenes=95; onlyPartsScenes=95; partCount=48; oldAtlasFiles=0; textureBytes=$report.textureBytes; missingPartRejected=$true; corruptPartRejected=$true } # 当前正式与隔离验证证据
$summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $verificationRoot 'skeletal-resource-report.json') -Encoding utf8
Write-Output '验证通过：95个正式GPU场景、仅48拆件的隔离工程95场景、异工作目录启动及部件缺失/损坏错误分支。'
