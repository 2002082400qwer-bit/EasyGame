# 构建C动作原型，无参数；编译器和raylib从本工程读取，不修改系统PATH。
$ErrorActionPreference = 'Stop' # 遇到PowerShell错误立即停止脚本
$projectRoot = $PSScriptRoot # 当前脚本所在的工程根目录
$compilerPath = Join-Path $projectRoot 'tools\w64devkit\bin\gcc.exe' # 工程内便携C编译器
$raylibRoot = Join-Path $projectRoot 'third_party\raylib-6.0_win64_mingw-w64' # 官方raylib预编译库目录
$buildDirectory = Join-Path $projectRoot 'build' # 构建输出目录
if (!(Test-Path -LiteralPath $compilerPath) -or !(Test-Path -LiteralPath (Join-Path $raylibRoot 'lib\libraylib.a'))) {
    throw '缺少便携编译器或raylib，请先执行tools\setup.ps1。'
}
New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null
$compileArguments = @( # 游戏编译及静态链接参数
    '-std=c99', '-Wall', '-Wextra', '-Werror', '-O2', '-static',
    '-finput-charset=UTF-8', '-fexec-charset=UTF-8',
    '-I', (Join-Path $projectRoot 'src'), '-I', (Join-Path $raylibRoot 'include'),
    (Join-Path $projectRoot 'src\main.c'), (Join-Path $projectRoot 'src\player.c'),
    '-L', (Join-Path $raylibRoot 'lib'), '-lraylib', '-lopengl32', '-lgdi32', '-lwinmm',
    '-o', (Join-Path $buildDirectory 'MX002.exe')
)
& $compilerPath @compileArguments
if ($LASTEXITCODE -ne 0) { throw '游戏构建失败。' }
Write-Output ('构建完成：' + (Join-Path $buildDirectory 'MX002.exe'))
