param(
    [string]$QtRoot = 'F:\Qt\5.15.2\mingw81_64',
    [string]$MinGWRoot = 'F:\Qt\Tools\mingw810_64',
    [switch]$Verify
)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$buildRoot = Join-Path $projectRoot 'build'
$stagedSource = Join-Path $buildRoot 'src'
$qmakePath = Join-Path $QtRoot 'bin\qmake.exe'
$makePath = Join-Path $MinGWRoot 'bin\mingw32-make.exe'
if (-not (Test-Path -LiteralPath $qmakePath) -or -not (Test-Path -LiteralPath $makePath)) {
    throw 'Qt / MinGW not found. Specify -QtRoot and -MinGWRoot.'
}
New-Item -ItemType Directory -Path $stagedSource -Force | Out-Null
Copy-Item -Path (Join-Path $projectRoot '源码\*') -Destination $stagedSource -Force
$originalDirectory = (Get-Location).Path
$originalPath = $env:Path
try {
    $env:Path = (Join-Path $MinGWRoot 'bin') + ';' + (Join-Path $QtRoot 'bin') + ';' + $env:Path
    Set-Location -LiteralPath $buildRoot
    & $qmakePath 'src\ArduPilotLogAnalyzer.pro'
    if ($LASTEXITCODE -ne 0) { throw 'qmake failed.' }
    $resourcePath = Join-Path $buildRoot 'ArduPilotLogAnalyzer_resource.rc'
    if (Test-Path -LiteralPath $resourcePath) {
        $fileSystem = New-Object -ComObject Scripting.FileSystemObject
        $iconShortPath = $fileSystem.GetFile((Join-Path $stagedSource 'flight.ico')).ShortPath
        $resourceText = Get-Content -LiteralPath $resourcePath -Raw
        $resourceText = [regex]::Replace($resourceText, '"[^"\r\n]*flight\.ico"', '"' + $iconShortPath.Replace('\', '\\') + '"')
        [System.IO.File]::WriteAllText($resourcePath, $resourceText, [System.Text.UTF8Encoding]::new($false))
    }
    & $makePath -j4
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
    Copy-Item -LiteralPath (Join-Path $buildRoot 'release\ArduPilotLogAnalyzer.exe') -Destination (Join-Path $projectRoot '飞析.exe') -Force
    foreach ($name in @('Qt5Core.dll','Qt5Gui.dll','Qt5Widgets.dll','Qt5PrintSupport.dll')) {
        Copy-Item -LiteralPath (Join-Path $QtRoot ('bin\' + $name)) -Destination $projectRoot -Force
    }
    foreach ($name in @('libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll')) {
        Copy-Item -LiteralPath (Join-Path $MinGWRoot ('bin\' + $name)) -Destination $projectRoot -Force
    }
    $platformDirectory = Join-Path $projectRoot 'platforms'
    New-Item -ItemType Directory -Path $platformDirectory -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $QtRoot 'plugins\platforms\qwindows.dll') -Destination $platformDirectory -Force
    Write-Host 'Built: 飞析.exe (runtime dependencies included)' 
    if ($Verify) {
        Set-Location -LiteralPath $projectRoot
        & python '测试\generate_fixtures.py'
        if ($LASTEXITCODE -ne 0) { throw 'Fixture generation failed.' }
        $previousQtPlatform = $env:QT_QPA_PLATFORM
        try {
            $env:QT_QPA_PLATFORM = 'windows'
            $testProcess = Start-Process -FilePath (Join-Path $projectRoot '飞析.exe') -ArgumentList '--self-test','测试','验证/回归验证.json' -WindowStyle Hidden -Wait -PassThru
            if ($testProcess.ExitCode -ne 0) { throw 'Regression failed. See 验证/回归验证.json.' }
            Write-Host 'Regression passed. See 验证/回归验证.json.'
        } finally { $env:QT_QPA_PLATFORM = $previousQtPlatform }
    }
} finally {
    $env:Path = $originalPath
    Set-Location -LiteralPath $originalDirectory
}
