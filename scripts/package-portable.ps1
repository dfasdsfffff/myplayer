param(
    [string]$BuildDir = "build",
    [string]$Configuration = "Release",
    [string]$Version = "1.0.0",
    [string]$QtRoot = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$buildPath = Join-Path $root $BuildDir
$binPath = Join-Path $root "bin"
$distPath = Join-Path $root "dist"
# Default output: MyPlayer-1.0.0-windows-x64.zip
$packageName = "MyPlayer-$Version-windows-x64"
$stagePath = Join-Path $distPath $packageName
$zipPath = Join-Path $distPath "$packageName.zip"
$exePath = Join-Path $binPath "myplayer.exe"

if ($env:PATH -and $env:Path -and $env:PATH -ne $env:Path) {
    $env:Path = $env:Path
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
} elseif ($env:PATH -and -not $env:Path) {
    $env:Path = $env:PATH
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
}

function Read-CMakeCacheValue {
    param(
        [string]$CachePath,
        [string]$Name
    )

    if (-not (Test-Path -LiteralPath $CachePath)) {
        return $null
    }

    foreach ($line in Get-Content -LiteralPath $CachePath) {
        if ($line -match "^$([regex]::Escape($Name)):[^=]+=(.*)$") {
            return $Matches[1]
        }
    }

    return $null
}

function Copy-IfExists {
    param(
        [string]$Path,
        [string]$Destination
    )

    if (Test-Path -LiteralPath $Path) {
        Copy-Item -LiteralPath $Path -Destination $Destination -Force
    }
}

function Copy-RequiredFile {
    param(
        [string]$Path,
        [string]$Destination
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Required file not found: $Path"
    }

    Copy-Item -LiteralPath $Path -Destination $Destination -Force
}

function Compress-WithRetry {
    param(
        [string]$SourceGlob,
        [string]$DestinationPath
    )

    $lastError = $null
    for ($attempt = 1; $attempt -le 5; ++$attempt) {
        try {
            Compress-Archive -Path $SourceGlob -DestinationPath $DestinationPath -Force
            return
        } catch {
            $lastError = $_
            Start-Sleep -Seconds 2
        }
    }

    throw $lastError
}

function Invoke-CleanCMakeBuild {
    param(
        [string]$BuildPath,
        [string]$Configuration
    )

    $escapedBuildPath = $BuildPath.Replace('"', '\"')
    $escapedConfiguration = $Configuration.Replace('"', '\"')
    $pathValue = $env:Path
    if ([string]::IsNullOrWhiteSpace($pathValue)) {
        $pathValue = $env:PATH
    }

    $command = "set `"PATH=`" & set `"Path=$pathValue`" & cmake --build `"$escapedBuildPath`" --config `"$escapedConfiguration`" --target myplayer"
    cmd.exe /d /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "Release build failed with exit code $LASTEXITCODE."
    }
}

Invoke-CleanCMakeBuild -BuildPath $buildPath -Configuration $Configuration

if (-not (Test-Path -LiteralPath $exePath)) {
    throw "Expected Release executable not found: $exePath"
}

$rootPath = [System.IO.Path]::GetFullPath($root)
$stageFullPath = [System.IO.Path]::GetFullPath($stagePath)
if (-not $stageFullPath.StartsWith($rootPath, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to package outside workspace: $stageFullPath"
}

if (Test-Path -LiteralPath $stagePath) {
    Remove-Item -LiteralPath $stagePath -Recurse -Force
}
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

New-Item -ItemType Directory -Path $stagePath | Out-Null

Copy-RequiredFile -Path $exePath -Destination $stagePath

$cachePath = Join-Path $buildPath "CMakeCache.txt"
$vcpkgInstalledDir = Read-CMakeCacheValue -CachePath $cachePath -Name "VCPKG_INSTALLED_DIR"
$vcpkgTriplet = Read-CMakeCacheValue -CachePath $cachePath -Name "VCPKG_TARGET_TRIPLET"
if ([string]::IsNullOrWhiteSpace($vcpkgInstalledDir) -or [string]::IsNullOrWhiteSpace($vcpkgTriplet)) {
    throw "vcpkg dependency paths are unavailable. Configure the project with the vcpkg toolchain first."
}

$vcpkgBin = Join-Path (Join-Path $vcpkgInstalledDir $vcpkgTriplet) "bin"
if (-not (Test-Path -LiteralPath $vcpkgBin)) {
    throw "vcpkg runtime directory not found: $vcpkgBin"
}

Get-ChildItem -LiteralPath $vcpkgBin -Filter "*.dll" | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $stagePath -Force
}

if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    $QtRoot = Read-CMakeCacheValue -CachePath $cachePath -Name "PLAYERDEMO_QT_ROOT"
}
if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    $qt6Dir = Read-CMakeCacheValue -CachePath $cachePath -Name "Qt6_DIR"
    if (-not [string]::IsNullOrWhiteSpace($qt6Dir)) {
        $QtRoot = Resolve-Path -LiteralPath (Join-Path $qt6Dir "..\..\..")
    }
}

$windeployqt = $null
if (-not [string]::IsNullOrWhiteSpace($QtRoot)) {
    $candidate = Join-Path $QtRoot "bin\windeployqt.exe"
    if (Test-Path -LiteralPath $candidate) {
        $windeployqt = $candidate
    }
}
if (-not $windeployqt) {
    $command = Get-Command windeployqt -ErrorAction SilentlyContinue
    if ($command) {
        $windeployqt = $command.Source
    }
}
if (-not $windeployqt) {
    throw "windeployqt not found. Pass -QtRoot or configure PLAYERDEMO_QT_ROOT."
}

& $windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw (Join-Path $stagePath "myplayer.exe")
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE."
}

$qtPlatformPlugin = Join-Path $stagePath "platforms\qwindows.dll"
if (-not (Test-Path -LiteralPath $qtPlatformPlugin)) {
    throw "Qt platform plugin was not deployed: platforms\qwindows.dll"
}

Copy-RequiredFile -Path (Join-Path $root "LICENSE") -Destination $stagePath
Copy-RequiredFile -Path (Join-Path $root "NOTICE") -Destination $stagePath
Copy-RequiredFile -Path (Join-Path $root "README.md") -Destination $stagePath
Copy-RequiredFile -Path (Join-Path $root "THIRD-PARTY-NOTICES.md") -Destination $stagePath
Copy-IfExists -Path (Join-Path $root "docs\release-checklist.md") -Destination $stagePath

Compress-WithRetry -SourceGlob (Join-Path $stagePath "*") -DestinationPath $zipPath

Write-Host "Portable package created: $zipPath"
