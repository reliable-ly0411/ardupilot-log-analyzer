param(
    [string]$Destination = '',
    [switch]$SkipArchive
)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Destination)) { $Destination = Join-Path $projectRoot '成品\飞析3.0' }
$Destination = [System.IO.Path]::GetFullPath($Destination)
if (Test-Path -LiteralPath $Destination) { throw '成品目录已经存在。请用 -Destination 指定新的空目录，以免覆盖已有试飞记录。' }
$runtime = @('飞析.exe','Qt5Core.dll','Qt5Gui.dll','Qt5Widgets.dll','Qt5PrintSupport.dll','libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll')
foreach ($name in $runtime) { if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $name))) { throw ('缺少运行文件：' + $name) } }
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
foreach ($name in $runtime) { Copy-Item -LiteralPath (Join-Path $projectRoot $name) -Destination $Destination }
$platformDirectory = Join-Path $Destination 'platforms'
New-Item -ItemType Directory -Path $platformDirectory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'platforms\qwindows.dll') -Destination $platformDirectory
foreach ($name in @('示例日志','运行库许可')) { Copy-Item -LiteralPath (Join-Path $projectRoot $name) -Destination $Destination -Recurse }
$instructions = [System.IO.File]::ReadAllText((Join-Path $projectRoot '使用说明.md'),[System.Text.Encoding]::UTF8)
$index = $instructions.IndexOf('## 文件说明')
if ($index -ge 0) { $instructions = $instructions.Substring(0,$index) }
$instructions += "## 成品目录`r`n`r`n飞析.exe、DLL 和 platforms 为运行文件；示例日志包含教学日志及练习；运行库许可保留依赖说明。试飞记录和飞析设置.ini 将在使用后自动生成。请将整个文件夹放在有写入权限的目录。`r`n"
[System.IO.File]::WriteAllText((Join-Path $Destination '使用说明.md'),$instructions,[System.Text.UTF8Encoding]::new($false))
foreach ($name in @('先读我.txt','改造与验证记录.md')) { Copy-Item -LiteralPath (Join-Path $projectRoot $name) -Destination $Destination }
if (-not $SkipArchive) {
    $archive = Join-Path $projectRoot '飞析3.0_试飞复盘版.zip'
    Compress-Archive -LiteralPath $Destination -DestinationPath $archive -Force
    Write-Host ('运行包：' + $archive)
}
Write-Host ('成品目录：' + $Destination)
