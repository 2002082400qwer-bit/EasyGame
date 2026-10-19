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
$skeletonExecutable = Join-Path $projectRoot 'build\skeleton_tests.exe' # 不依赖图片或GPU的骨骼数学测试
& $compilerPath -std=c99 -Wall -Wextra -Werror -O2 -static -I (Join-Path $projectRoot 'src') (Join-Path $projectRoot 'tests\skeleton_tests.c') (Join-Path $projectRoot 'src\skeleton.c') -lm -o $skeletonExecutable
if ($LASTEXITCODE -ne 0) { throw '骨骼数学测试编译失败。' }
& $skeletonExecutable
if ($LASTEXITCODE -ne 0) { throw '骨骼数学测试失败。' }
$actorExecutable = Join-Path $projectRoot 'build\skeletal_actor_tests.exe' # 原35状态到实时姿势的适配回归
& $compilerPath -std=c99 -Wall -Wextra -Werror -O2 -static -I (Join-Path $projectRoot 'src') (Join-Path $projectRoot 'tests\skeletal_actor_tests.c') (Join-Path $projectRoot 'src\skeletal_actor.c') (Join-Path $projectRoot 'src\skeleton.c') (Join-Path $projectRoot 'src\animation.c') (Join-Path $projectRoot 'src\player.c') -lm -o $actorExecutable
if ($LASTEXITCODE -ne 0) { throw '实时动作适配测试编译失败。' }
& $actorExecutable
if ($LASTEXITCODE -ne 0) { throw '实时动作适配测试失败。' }
$motionExecutable = Join-Path $projectRoot 'build\motion_tests.exe' # 真实输入的连续动作、接地和只读渲染回归
& $compilerPath -std=c99 -Wall -Wextra -Werror -O2 -static -I (Join-Path $projectRoot 'src') (Join-Path $projectRoot 'tests\motion_tests.c') (Join-Path $projectRoot 'src\motion.c') (Join-Path $projectRoot 'src\skeletal_actor.c') (Join-Path $projectRoot 'src\skeleton.c') (Join-Path $projectRoot 'src\animation.c') (Join-Path $projectRoot 'src\player.c') -lm -o $motionExecutable
if ($LASTEXITCODE -ne 0) { throw '连续动作测试编译失败。' }
& $motionExecutable
if ($LASTEXITCODE -ne 0) { throw '连续动作测试失败。' }
$qualityExecutable = Join-Path $projectRoot 'build\action_quality_tests.exe' # 跑姿、移动反向与转向全宽回归
& $compilerPath -std=c99 -Wall -Wextra -Werror -O2 -static -I (Join-Path $projectRoot 'src') (Join-Path $projectRoot 'tests\action_quality_tests.c') (Join-Path $projectRoot 'src\motion.c') (Join-Path $projectRoot 'src\skeletal_actor.c') (Join-Path $projectRoot 'src\skeleton.c') (Join-Path $projectRoot 'src\animation.c') (Join-Path $projectRoot 'src\player.c') -lm -o $qualityExecutable
if ($LASTEXITCODE -ne 0) { throw '动作轮廓测试编译失败。' }
& $qualityExecutable
if ($LASTEXITCODE -ne 0) { throw '动作轮廓测试失败。' }
