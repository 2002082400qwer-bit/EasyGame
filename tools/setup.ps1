# 下载并解压工程内便携依赖，无参数；只调用归档解压工具，不执行工具链自解压启动器。
$ErrorActionPreference = 'Stop' # 遇到PowerShell错误立即停止脚本
$projectRoot = Split-Path -Parent $PSScriptRoot # tools目录上一级的工程根目录
$downloadDirectory = Join-Path $PSScriptRoot 'downloads' # 下载归档保存目录
$libraryDirectory = Join-Path $projectRoot 'third_party' # raylib解压目录
New-Item -ItemType Directory -Path $downloadDirectory, $libraryDirectory -Force | Out-Null

# 获取固定版本归档：Url为官方地址，Name为文件名，Sha256为本次验证过的内容摘要。
function Get-ProjectArchive {
    param(
        [string]$Url, # 归档文件的官方发布地址
        [string]$Name, # 下载到本工程的文件名
        [string]$Sha256 # 固定版本内容的SHA256摘要
    )
    $archivePath = Join-Path $downloadDirectory $Name # 归档本地绝对路径
    if (!(Test-Path -LiteralPath $archivePath)) {
        Invoke-WebRequest -Uri $Url -OutFile $archivePath
    }
    if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $Sha256) {
        throw ('下载内容与固定版本不一致：' + $Name)
    }
    return $archivePath
}

$raylibArchive = Get-ProjectArchive -Url 'https://github.com/raysan5/raylib/releases/download/6.0/raylib-6.0_win64_mingw-w64.zip' -Name 'raylib-6.0_win64_mingw-w64.zip' -Sha256 '69688E025812C8132634C609C30938EDA4A3FA14D63C4031108873A4D797E2D3' # raylib官方固定版本归档
$compilerArchive = Get-ProjectArchive -Url 'https://github.com/skeeto/w64devkit/releases/download/v2.10.0/w64devkit-x64-2.10.0.7z.exe' -Name 'w64devkit-x64-2.10.0.7z.exe' -Sha256 '18D0A4C71A166F8401AB6305781BEC5882B40B5E06BA9807C61CB5F3B3C6325E' # 便携GCC工具链归档
$extractor = Get-ProjectArchive -Url 'https://www.7-zip.org/a/7zr.exe' -Name '7zr.exe' -Sha256 '256FECA8E274E5DA655E2A284FABAFD9F554365EB164862089DACD4E8276D282' # 官方独立7z归档解压工具

if (!(Test-Path -LiteralPath (Join-Path $libraryDirectory 'raylib-6.0_win64_mingw-w64\lib\libraylib.a'))) {
    Expand-Archive -LiteralPath $raylibArchive -DestinationPath $libraryDirectory -Force
}
if (!(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'w64devkit\bin\gcc.exe'))) {
    & $extractor x $compilerArchive ('-o' + $PSScriptRoot) -y
    if ($LASTEXITCODE -ne 0) { throw '编译器归档解压失败。' }
}
Write-Output '工程便携依赖已就绪，可运行根目录build.ps1。'
