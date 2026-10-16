# 导出最小可运行骨骼版本；先执行build.ps1，包内只包含程序和48个固定拆件，无参数。
$ErrorActionPreference = 'Stop' # 文件复制或压缩失败立即停止
$projectRoot = $PSScriptRoot # 当前工程根目录
$executable = Join-Path $projectRoot 'build/MX002.exe' # 已验证的游戏程序
if (!(Test-Path -LiteralPath $executable)) { throw '请先执行build.ps1。' }
$staging = Join-Path $projectRoot ('build/runtime-package-' + [guid]::NewGuid().ToString('N')) # 本次独立打包目录，避免混入旧资产
$binaryRoot = Join-Path $staging 'build' # 保持程序向上寻找工程根目录的约定
$partRoot = Join-Path $staging '图片/游戏素材/骨骼拆分方式' # 包内唯一的图片目录
New-Item -ItemType Directory -Path $binaryRoot,$partRoot -Force | Out-Null
Copy-Item -LiteralPath $executable -Destination (Join-Path $binaryRoot 'MX002.exe')
Copy-Item -LiteralPath (Join-Path $projectRoot '图片/游戏素材/骨骼拆分方式/parts') -Destination (Join-Path $partRoot 'parts') -Recurse
$instructions = '解压后双击build/MX002.exe。A/D走，Ctrl跑，空格跳，S蹲，Shift冲刺，1/2/3/4切换正/左/背/右，R重置，F1调试，Esc退出。请保留build与图片目录的关系。此包只含48拆件，姿势数据已编入程序。' # 包内用户说明
Set-Content -LiteralPath (Join-Path $staging '使用说明.txt') -Value $instructions -Encoding utf8
$package = Join-Path $projectRoot 'build/MX002-skeletal-runtime.zip' # 仅运行时文件的压缩包
Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $package -Force
Write-Output ('最小运行包：' + $package)
Write-Output ('包大小：{0:N2} MiB' -f ((Get-Item -LiteralPath $package).Length / 1MB))
