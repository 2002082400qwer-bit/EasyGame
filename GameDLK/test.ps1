# 编译并运行角色行为测试，无参数；返回非零退出码表示测试未通过。
$ErrorActionPreference = 'Stop' # 遇到PowerShell错误立即停止脚本
$projectRoot = $PSScriptRoot # 当前工程根目录
$compilerPath = Join-Path $projectRoot 'tools\w64devkit\bin\gcc.exe' # 工程内C编译器
$testExecutable = Join-Path $projectRoot 'build\player_tests.exe' # 行为测试可执行文件
New-Item -ItemType Directory -Path (Join-Path $projectRoot 'build') -Force | Out-Null
& $compilerPath -std=c99 -Wall -Wextra -Werror -O2 -static -I (Join-Path $projectRoot 'src') (Join-Path $projectRoot 'tests\player_tests.c') (Join-Path $projectRoot 'src\player.c') -o $testExecutable
if ($LASTEXITCODE -ne 0) { throw '行为测试编译失败。' }
& $testExecutable
if ($LASTEXITCODE -ne 0) { throw '行为测试失败。' }
$animationExecutable = Join-Path $projectRoot 'build\animation_tests.exe' # 独立动画行为测试程序
& $compilerPath -std=c99 -Wall -Wextra -Werror -O2 -static -I (Join-Path $projectRoot 'src') (Join-Path $projectRoot 'tests\animation_tests.c') (Join-Path $projectRoot 'src\animation.c') (Join-Path $projectRoot 'src\player.c') -o $animationExecutable
if ($LASTEXITCODE -ne 0) { throw '动画测试编译失败。' }
& $animationExecutable
if ($LASTEXITCODE -ne 0) { throw '动画行为测试失败。' }
