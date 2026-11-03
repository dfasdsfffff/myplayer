param(
    [string]$Player = "bin/myplayer.exe",
    [string]$Ffmpeg = "ffmpeg",
    [string]$OutputDirectory = ".cache/benchmarks",
    [ValidateRange(1, 3600000)][int]$RunMs = 10000,
    [ValidateRange(2, 3600000)][int]$TimeoutMs = 30000,
    [ValidateRange(10, 10000)][int]$SampleIntervalMs = 200,
    [switch]$SkipFixtureGeneration,
    [switch]$SkipDecodeReference
)

$ErrorActionPreference = "Stop"
if ($TimeoutMs -le $RunMs) { throw "TimeoutMs must be greater than RunMs" }
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$output = if ([IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory } else { Join-Path $root $OutputDirectory }
New-Item -ItemType Directory -Force -Path $output | Out-Null
$output = (Resolve-Path $output).Path
$playerCandidate = if (![IO.Path]::IsPathRooted($Player) -and (Test-Path (Join-Path $root $Player) -PathType Leaf)) {
    Join-Path $root $Player
} else { $Player }
$playerPath = (Get-Command $playerCandidate -CommandType Application -ErrorAction Stop).Source
$ffmpegPath = (Get-Command $Ffmpeg -CommandType Application -ErrorAction Stop).Source

# Start-Process joins ArgumentList into a native command line on Windows PowerShell
# 5.1. File paths cannot contain a quote on Windows; quote spaces explicitly.
function Quote-NativePath([string]$Value) {
    if ($Value.Contains('"')) { throw "A native path cannot contain a quote: $Value" }
    return '"' + $Value + '"'
}

$fixtures = @(
    @{ Name = "testsrc2-1080p60-10s"; Size = "1920x1080" },
    @{ Name = "testsrc2-4k60-10s"; Size = "3840x2160" }
)
$results = New-Object 'System.Collections.Generic.List[object]'
$failures = New-Object 'System.Collections.Generic.List[string]'

foreach ($fixture in $fixtures) {
    $media = Join-Path $output ("{0}.mp4" -f $fixture.Name)
    if (!$SkipFixtureGeneration) {
        & $ffmpegPath -hide_banner -y -f lavfi -i ("testsrc2=size={0}:rate=60" -f $fixture.Size) `
            -t 10 -an -c:v libx264 -preset ultrafast -crf 23 -pix_fmt yuv420p $media
        if ($LASTEXITCODE -ne 0) { throw "fixture generation failed: $($fixture.Name)" }
    }
    if (!(Test-Path $media -PathType Leaf)) { throw "fixture missing: $media" }

    # Separate reference only: ffmpeg null decoding is NOT the GUI benchmark.
    $decodeLog = $null
    if (!$SkipDecodeReference) {
        $decodeLog = Join-Path $output ("decode-reference-{0}.log" -f $fixture.Name)
        $decodeArguments = @("-hide_banner", "-benchmark", "-threads", "1", "-i", (Quote-NativePath $media), "-an", "-f", "null", "NUL")
        $decodeProcess = Start-Process -FilePath $ffmpegPath -ArgumentList $decodeArguments -PassThru -Wait `
            -RedirectStandardError $decodeLog -RedirectStandardOutput ($decodeLog + ".stdout")
        try {
            if ($decodeProcess.ExitCode -ne 0) { throw "decode reference failed: $($fixture.Name)" }
        } finally { $decodeProcess.Dispose() }
    }

    $reportPath = Join-Path $output ("player-{0}.json" -f $fixture.Name)
    $stdoutPath = Join-Path $output ("player-{0}.stdout.log" -f $fixture.Name)
    $stderrPath = Join-Path $output ("player-{0}.stderr.log" -f $fixture.Name)
    $samplesPath = Join-Path $output ("process-{0}.json" -f $fixture.Name)
    # Never accidentally consume a report from an older run after a crash.
    if (Test-Path $reportPath) { Remove-Item $reportPath }
    $arguments = @(
        "--benchmark", "--benchmark-media", (Quote-NativePath $media),
        "--benchmark-output", (Quote-NativePath $reportPath),
        "--benchmark-run-ms", "$RunMs", "--benchmark-timeout-ms", "$TimeoutMs"
    )
    $samples = New-Object 'System.Collections.Generic.List[object]'
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $playerPath -ArgumentList $arguments -PassThru `
        -WorkingDirectory (Split-Path $playerPath) -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    # Retain the native handle before polling: PS5.1 otherwise loses ExitCode
    # when Start-Process returns a process that has already been reaped.
    $null = $process.Handle
    $killed = $false
    $samplingError = $null
    $lastCpuMs = $null
    $lastCpuSampleMs = $null
    $peakWorkingSet = 0L
    $peakPrivate = 0L
    try {
        while (!$process.HasExited) {
            if ($clock.ElapsedMilliseconds -gt ($TimeoutMs + 5000)) {
                $process.Kill()
                $killed = $true
                break
            }
            try {
                $process.Refresh()
                $cpuMs = $process.TotalProcessorTime.TotalMilliseconds
                $workingSet = $process.WorkingSet64
                $privateBytes = $process.PrivateMemorySize64
                $atMs = $clock.Elapsed.TotalMilliseconds
                $lastCpuMs = $cpuMs
                $lastCpuSampleMs = $atMs
                $peakWorkingSet = [Math]::Max($peakWorkingSet, $workingSet)
                $peakPrivate = [Math]::Max($peakPrivate, $privateBytes)
                $samples.Add([ordered]@{
                    elapsed_ms = $atMs
                    cumulative_process_cpu_ms = $cpuMs
                    working_set_bytes = $workingSet
                    private_bytes = $privateBytes
                })
            } catch {
                # Exit can race a sample. Preserve previous samples honestly.
                if (!$process.HasExited) { $samplingError = $_.Exception.Message }
            }
            Start-Sleep -Milliseconds $SampleIntervalMs
        }
        if (!$process.WaitForExit(5000)) { throw "player did not terminate after process kill" }
        $exitCode = $process.ExitCode
        $elapsedMs = $clock.Elapsed.TotalMilliseconds
        try {
            $finalCpuMs = $process.TotalProcessorTime.TotalMilliseconds
            if ($null -ne $finalCpuMs) {
                $lastCpuMs = $finalCpuMs
                $lastCpuSampleMs = $elapsedMs
            }
        } catch { }
    } finally {
        if (!$process.HasExited) { $process.Kill() }
        $process.Dispose()
        $clock.Stop()
    }

    ConvertTo-Json -InputObject @($samples.ToArray()) -Depth 5 | Set-Content -Encoding UTF8 $samplesPath
    $playerReport = $null
    $reportError = $null
    try {
        if (!(Test-Path $reportPath -PathType Leaf)) { throw "player report missing" }
        $playerReport = Get-Content -Raw $reportPath | ConvertFrom-Json
        if ($playerReport.schema_version -ne 1 -or $playerReport.benchmark -ne "myplayer_qt_sdl_playback") {
            throw "unexpected player report schema"
        }
    } catch { $reportError = $_.Exception.Message }
    $averageCpu = $null
    if ($null -ne $lastCpuMs -and $lastCpuSampleMs -gt 0) {
        $averageCpu = 100.0 * $lastCpuMs / $lastCpuSampleMs
    }
    $result = [ordered]@{
        fixture = $fixture.Name
        media = $media
        player_report_path = $reportPath
        player = $playerReport
        player_report_error = $reportError
        process_exit_code = $exitCode
        process_killed_by_script = $killed
        launch_to_exit_observed_ms = $elapsedMs
        observed_process_cpu_ms = $lastCpuMs
        cpu_observation_through_ms = $lastCpuSampleMs
        average_process_cpu_percent_one_core = $averageCpu
        logical_processor_count = [Environment]::ProcessorCount
        sampled_peak_working_set_bytes = if ($samples.Count -gt 0) { $peakWorkingSet } else { $null }
        sampled_peak_private_bytes = if ($samples.Count -gt 0) { $peakPrivate } else { $null }
        process_sample_count = $samples.Count
        process_samples_path = $samplesPath
        sampling_error = $samplingError
        decode_reference_log = $decodeLog
    }
    $results.Add($result)
    if ($exitCode -ne 0 -or $killed -or $null -ne $reportError -or
        @("run_completed", "end_of_media") -notcontains $playerReport.outcome) {
        $failures.Add($fixture.Name)
    }
}

$summary = [ordered]@{
    schema_version = 1
    benchmark = "myplayer_gui_process_baseline"
    player_executable = $playerPath
    ffmpeg_executable = $ffmpegPath
    sample_interval_ms = $SampleIntervalMs
    cpu_definition = "Cumulative player-process CPU time; average percent uses one logical core as 100%, not normalized to machine capacity. Observation ends at final available CPU sample."
    memory_definition = "Maxima of periodic process working-set/private-byte samples, not allocator or GPU-memory peaks."
    exit_timing_definition = "Script stopwatch from immediately before Start-Process to observed exit; includes launch and polling delay."
    fixture_definition = "10-second 60fps H.264 video-only testsrc2; no A/V synchronization or dropped-frame claim."
    results = @($results.ToArray())
}
$summaryPath = Join-Path $output "baseline.json"
$summary | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 $summaryPath
Write-Host "GUI-player baseline: $summaryPath"
if ($failures.Count -gt 0) { throw "GUI-player benchmark failed: $($failures -join ', '); see baseline.json and player logs" }
