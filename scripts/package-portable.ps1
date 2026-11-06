param(
    [string]$BuildDir = "build",
    [ValidateSet("Release")][string]$Configuration = "Release",
    [string]$Version = "1.1.0",
    [string]$QtLicenseDir,
    [string]$CrtDir
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$buildPath = (Resolve-Path (Join-Path $root $BuildDir)).Path
$distPath = Join-Path $root "dist"
$cache = Get-Content -LiteralPath (Join-Path $buildPath "CMakeCache.txt")

function Get-CacheValue([string]$Name) {
    $line = $cache | Where-Object { $_ -match "^$([regex]::Escape($Name)):[^=]+=" } | Select-Object -First 1
    if (-not $line) { throw "Missing CMake cache entry: $Name" }
    return ($line -split "=", 2)[1]
}

function Assert-RequiredPath([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "Required release content is missing: $Path" }
}

$qtRoot = Get-CacheValue "PLAYERDEMO_QT_ROOT"
$dependencyRoot = Join-Path (Get-CacheValue "VCPKG_INSTALLED_DIR") (Get-CacheValue "VCPKG_TARGET_TRIPLET")
if (-not $QtLicenseDir) { $QtLicenseDir = Join-Path (Split-Path $qtRoot) "Src\LICENSES" }
if (-not $CrtDir) {
    $vsRoot = Get-CacheValue "CMAKE_GENERATOR_INSTANCE"
    $CrtDir = Get-ChildItem "$vsRoot/VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT" -Directory |
        Sort-Object { [version]$_.Parent.Parent.Name } -Descending | Select-Object -First 1 -ExpandProperty FullName
}
foreach ($required in @($QtLicenseDir, $CrtDir, "$qtRoot/bin/windeployqt.exe", "$dependencyRoot/share")) { Assert-RequiredPath $required }
if ((Get-CacheValue "MYPLAYER_BUILD_MONITOR") -ne "ON") { throw "Configure with -DMYPLAYER_BUILD_MONITOR=ON before packaging." }

cmake --build $buildPath --config $Configuration --target myplayer myplayer_monitor
if ($LASTEXITCODE -ne 0) { throw "Release build failed with exit code $LASTEXITCODE." }
New-Item -ItemType Directory -Force -Path $distPath | Out-Null

foreach ($app in @(
    @{ Name = "MyPlayer"; Exe = "myplayer.exe"; Arguments = @("--smoke-test") },
    @{ Name = "MyPlayerMonitor"; Exe = "myplayer_monitor.exe"; Arguments = @("--smoke-test", "--layout", "16") }
)) {
    $packageName = "$($app.Name)-$Version-windows-x64"
    $stagePath = Join-Path $distPath $packageName
    $zipPath = Join-Path $distPath "$packageName.zip"
    if ((Test-Path $stagePath) -or (Test-Path $zipPath)) { throw "Release output already exists: $packageName. Choose a new version or move the previous output." }
    cmake --install $buildPath --config $Configuration --prefix $stagePath
    if ($LASTEXITCODE -ne 0) { throw "Release install failed with exit code $LASTEXITCODE." }
    $binPath = Join-Path $stagePath "bin"
    if ($app.Name -eq "MyPlayerMonitor") {
        $monitorExe = Join-Path (Get-CacheValue "PLAYERDEMO_OUTPUT_DIR") "myplayer_monitor.exe"
        Assert-RequiredPath $monitorExe
        Copy-Item -LiteralPath $monitorExe -Destination $binPath
        Remove-Item -LiteralPath (Join-Path $binPath "myplayer.exe")
    }
    $exePath = Join-Path $binPath $app.Exe
    & "$qtRoot/bin/windeployqt.exe" --release --no-translations --no-compiler-runtime $exePath
    if ($LASTEXITCODE -ne 0) { throw "Qt deployment failed with exit code $LASTEXITCODE." }
    Copy-Item "$CrtDir/*.dll" -Destination $binPath
    Set-Content -LiteralPath (Join-Path $binPath "qt.conf") -Value "[Paths]`nPrefix=.`nPlugins=." -Encoding ASCII

    $licenses = Join-Path $stagePath "licenses"
    foreach ($copyright in Get-ChildItem "$dependencyRoot/share/*/copyright" -File) {
        Copy-Item -LiteralPath $copyright.FullName -Destination (Join-Path $licenses "$($copyright.Directory.Name)-copyright")
    }
    Copy-Item -LiteralPath $QtLicenseDir -Destination (Join-Path $licenses "Qt") -Recurse
    foreach ($required in @($exePath, "$binPath/platforms/qwindows.dll", "$binPath/vcruntime140.dll", "$binPath/msvcp140.dll", "$stagePath/LICENSE", "$licenses/ffmpeg-copyright", "$licenses/libass-copyright", "$licenses/Qt/LGPL-3.0-only.txt")) { Assert-RequiredPath $required }
    $debugDlls = Get-ChildItem -LiteralPath $binPath -Recurse -File -Filter "*.dll" | Where-Object { $_.Name -match '^(Qt6.+d|msvcp\d+d|vcruntime\d+(?:_\d+)?d|ucrtbased|SDL2d|freetyped|libpng16d|bz2d|zd)\.dll$' }
    if ($debugDlls) { throw "Release contains Debug DLLs: $($debugDlls.Name -join ', ')" }

    # Remove development search paths so the smoke run uses the shipped dependencies.
    $oldPath = $env:PATH
    $oldPlugins = $env:QT_PLUGIN_PATH
    $oldPlatforms = $env:QT_QPA_PLATFORM_PLUGIN_PATH
    try {
        $env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
        $env:QT_PLUGIN_PATH = $binPath
        $env:QT_QPA_PLATFORM_PLUGIN_PATH = "$binPath/platforms"
        $process = Start-Process -FilePath $exePath -ArgumentList $app.Arguments -WorkingDirectory $binPath -PassThru
        if (-not $process.WaitForExit(20000)) { $process.Kill(); throw "$($app.Name) smoke test timed out." }
        if ($process.ExitCode -ne 0) { throw "$($app.Name) staged smoke test failed with exit code $($process.ExitCode)." }
    } finally {
        $env:PATH = $oldPath
        $env:QT_PLUGIN_PATH = $oldPlugins
        $env:QT_QPA_PLATFORM_PLUGIN_PATH = $oldPlatforms
    }
    Compress-Archive -Path (Join-Path $stagePath "*") -DestinationPath $zipPath
    $hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -LiteralPath "$zipPath.sha256" -Value "$hash  $([IO.Path]::GetFileName($zipPath))" -NoNewline -Encoding ASCII
    Write-Host "Portable package verified: $zipPath"
}
