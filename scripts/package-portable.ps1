param(
    [string]$BuildDir = "build",
    [string]$Configuration = "Release",
    [string]$Version = "1.0.0"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$buildPath = Join-Path $root $BuildDir
$distPath = Join-Path $root "dist"
$packageName = "MyPlayer-$Version-windows-x64"
$stagePath = Join-Path $distPath $packageName
$zipPath = Join-Path $distPath "$packageName.zip"
$hashPath = "$zipPath.sha256"

function Assert-RequiredPath([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "Required staged content is missing: $Path" }
}

cmake --build $buildPath --config $Configuration --target myplayer
if ($LASTEXITCODE -ne 0) { throw "Release build failed with exit code $LASTEXITCODE." }
if (Test-Path -LiteralPath $stagePath) { Remove-Item -LiteralPath $stagePath -Recurse -Force }
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
if (Test-Path -LiteralPath $hashPath) { Remove-Item -LiteralPath $hashPath -Force }

cmake --install $buildPath --config $Configuration --prefix $stagePath
if ($LASTEXITCODE -ne 0) { throw "Release install failed with exit code $LASTEXITCODE." }

$exePath = Join-Path $stagePath "bin\myplayer.exe"
foreach ($required in @($exePath, (Join-Path $stagePath "bin\platforms\qwindows.dll"), (Join-Path $stagePath "LICENSE"), (Join-Path $stagePath "NOTICE"), (Join-Path $stagePath "licenses"))) { Assert-RequiredPath $required }
$debugDlls = Get-ChildItem -LiteralPath $stagePath -Recurse -File -Filter "*d.dll"
if ($debugDlls) { throw "Release stage contains Debug DLLs: $($debugDlls.FullName -join ', ')" }

$process = Start-Process -FilePath $exePath -ArgumentList "--smoke-test" -WorkingDirectory (Split-Path $exePath) -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "Staged smoke test failed with exit code $($process.ExitCode)." }

New-Item -ItemType Directory -Force -Path $distPath | Out-Null
Compress-Archive -Path (Join-Path $stagePath "*") -DestinationPath $zipPath -Force
$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath $hashPath -Value "$hash  $([System.IO.Path]::GetFileName($zipPath))" -NoNewline
Write-Host "Portable package created: $zipPath"
