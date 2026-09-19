param(
    [string]$Ffmpeg = "ffmpeg",
    [string]$OutputDirectory = ".cache/benchmarks"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$output = Join-Path $root $OutputDirectory
New-Item -ItemType Directory -Force -Path $output | Out-Null

$fixtures = @(
    @{ Name = "testsrc2-1080p60-10s"; Size = "1920x1080" },
    @{ Name = "testsrc2-4k60-10s"; Size = "3840x2160" }
)

foreach ($fixture in $fixtures) {
    $media = Join-Path $output ("{0}.mp4" -f $fixture.Name)
    $log = Join-Path $output ("decode-{0}.log" -f $fixture.Name.Replace("testsrc2-", ""))
    & $Ffmpeg -hide_banner -y -f lavfi -i ("testsrc2=size={0}:rate=60" -f $fixture.Size) `
        -t 10 -an -c:v libx264 -preset ultrafast -crf 23 -pix_fmt yuv420p $media
    if ($LASTEXITCODE -ne 0) { throw "fixture generation failed: $($fixture.Name)" }

    & $Ffmpeg -hide_banner -benchmark -threads 1 -i $media -an -f null NUL 2>&1 |
        Tee-Object -FilePath $log
    if ($LASTEXITCODE -ne 0) { throw "decode benchmark failed: $($fixture.Name)" }
}
