param(
    [string]$BuildDir = "build",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")][string]$Configuration = "Release",
    [string]$Ffmpeg = "ffmpeg",
    [string]$OutputDirectory = ".cache/media-compatibility"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
function Absolute-Path([string]$Value) {
    if ([IO.Path]::IsPathRooted($Value)) { return $Value }
    return Join-Path $root $Value
}
function Quote-NativeArgument([string]$Value) {
    # All arguments here are file paths or controlled encoder options, not shell commands.
    if ($Value.Contains('"')) { throw "Native argument contains a quote: $Value" }
    return '"' + $Value + '"'
}
function Find-Runner([string]$Name) {
    $candidates = @(
        (Join-Path $build ("tests/{0}/{1}/{1}.exe" -f $Configuration, $Name)),
        (Join-Path $build ("tests/{0}/{0}.exe" -f $Name)),
        (Join-Path $build ("{0}/{1}.exe" -f $Configuration, $Name)),
        (Join-Path $build ($Name + ".exe"))
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate -PathType Leaf) { return (Resolve-Path $candidate).Path }
    }
    throw "Cannot find $Name under $build ($Configuration). Build both acceptance targets first."
}

$build = Absolute-Path $BuildDir
$runtimeRunner = Find-Runner "media_compatibility_tests"
$hardwareRunner = Find-Runner "hardware_decode_policy_tests"
$ffmpegCandidate = if (Test-Path (Absolute-Path $Ffmpeg) -PathType Leaf) { Absolute-Path $Ffmpeg } else { $Ffmpeg }
$ffmpegPath = (Get-Command $ffmpegCandidate -CommandType Application -ErrorAction Stop).Source
$outputRoot = Absolute-Path $OutputDirectory
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
# Do not overwrite fixtures/reports while another invocation is using them.
$runName = (Get-Date -Format "yyyyMMdd-HHmmss") + "-" + [Guid]::NewGuid().ToString("N")
$output = Join-Path $outputRoot $runName
New-Item -ItemType Directory -Path $output | Out-Null
$output = (Resolve-Path $output).Path
$processes = New-Object 'System.Collections.Generic.List[object]'
$results = New-Object 'System.Collections.Generic.List[object]'
$skipped = New-Object 'System.Collections.Generic.List[object]'
$fixtures = New-Object 'System.Collections.Generic.List[object]'

function Invoke-BoundedProcess([string]$Executable, [string[]]$Arguments, [string]$Name, [int]$TimeoutMs = 45000) {
    $stdout = Join-Path $output ($Name + ".stdout.log")
    $stderr = Join-Path $output ($Name + ".stderr.log")
    $nativeArguments = @($Arguments | ForEach-Object { Quote-NativeArgument $_ })
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $Executable -ArgumentList $nativeArguments -PassThru `
        -WorkingDirectory (Split-Path $Executable) -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    # PS5.1 must retain Handle before HasExited polling, even for very short processes.
    $null = $process.Handle
    $killed = $false
    try {
        while (!$process.HasExited) {
            if ($clock.ElapsedMilliseconds -ge $TimeoutMs) { $process.Kill(); $killed = $true; break }
            Start-Sleep -Milliseconds 25
        }
        if (!$process.WaitForExit(5000)) { throw "$Name did not exit after kill" }
        $exitCode = $process.ExitCode
        if ($null -eq $exitCode) { throw "$Name returned no exit code" }
    } finally {
        if (!$process.HasExited) { $process.Kill() }
        $process.Dispose()
        $clock.Stop()
    }
    $record = [pscustomobject][ordered]@{
        name = $Name; executable = $Executable; arguments = $Arguments
        exit_code = $exitCode; killed_by_deadline = $killed
        observed_elapsed_ms = $clock.Elapsed.TotalMilliseconds
        stdout_log = $stdout; stderr_log = $stderr
    }
    $processes.Add($record)
    return $record
}

function Generate-Fixture([string]$Name, [string[]]$Arguments) {
    $process = Invoke-BoundedProcess $ffmpegPath (@("-hide_banner", "-nostdin", "-y") + $Arguments) ("generate-" + $Name) 90000
    if ($process.exit_code -ne 0 -or $process.killed_by_deadline) { throw "Fixture generation failed: $Name; see $($process.stderr_log)" }
}
function Add-Fixture([string]$Name, [string]$File, [string]$Expectation, [int]$Frames,
                     [int]$Width, [int]$Height, [string]$VideoCodec, [string]$AudioCodec) {
    $fixtures.Add([pscustomobject][ordered]@{
        name = $Name; media = (Join-Path $output $File); expected = $Expectation
        expected_frames = $Frames; expected_width = $Width; expected_height = $Height
        expected_video_codec = $VideoCodec; expected_audio_codec = $AudioCodec
    })
}

$gpuInventory = @()
$gpuInventoryError = $null
try {
    $gpuInventory = @(Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion, PNPDeviceID, AdapterCompatibility)
} catch { $gpuInventoryError = $_.Exception.Message }
$generatorVersion = Invoke-BoundedProcess $ffmpegPath @("-version") "ffmpeg-version"
if ($generatorVersion.exit_code -ne 0) { throw "Cannot obtain external ffmpeg version" }
$encodersProcess = Invoke-BoundedProcess $ffmpegPath @("-hide_banner", "-encoders") "ffmpeg-encoders"
if ($encodersProcess.exit_code -ne 0) { throw "Cannot obtain external ffmpeg encoder list" }
$encoders = (Get-Content -Raw $encodersProcess.stdout_log) + (Get-Content -Raw $encodersProcess.stderr_log)
foreach ($required in @("libx264", "libx265", "mpeg4", "aac", "pcm_s16le")) {
    if ($encoders -notmatch ("(?m)^\s*[A-Z\.]{6}\s+" + [regex]::Escape($required) + "\s")) {
        throw "Required fixture encoder unavailable: $required (choose -Ffmpeg with libx264/libx265/AAC support)"
    }
}

# 12 known 10fps frames. 8-bit 4:2:0 Main profiles avoid accidental unsupported
# 10-bit/4:4:4 hardware fixtures. These are acceptance clips, not performance probes.
$h264 = Join-Path $output "h264-aac.mp4"
Generate-Fixture "h264-aac" @("-f", "lavfi", "-i", "testsrc2=size=1280x720:rate=10:duration=1.2",
    "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=1.2",
    "-map", "0:v:0", "-map", "1:a:0", "-frames:v", "12", "-t", "1.2", "-c:v", "libx264",
    "-preset", "fast", "-profile:v", "main", "-pix_fmt", "yuv420p", "-threads", "1", "-c:a", "aac", $h264)
Add-Fixture "h264-aac" "h264-aac.mp4" "av" 12 1280 720 "h264" "aac"
$h265 = Join-Path $output "h265.mkv"
Generate-Fixture "h265" @("-f", "lavfi", "-i", "testsrc2=size=1280x720:rate=10:duration=1.2",
    "-frames:v", "12", "-an", "-c:v", "libx265", "-preset", "fast", "-profile:v", "main",
    "-pix_fmt", "yuv420p", "-threads", "1", "-x265-params", "pools=1:frame-threads=1", $h265)
Add-Fixture "h265" "h265.mkv" "video" 12 1280 720 "hevc" ""
$mpeg4 = Join-Path $output "mpeg4.avi"
Generate-Fixture "mpeg4" @("-f", "lavfi", "-i", "testsrc2=size=320x240:rate=10:duration=1.2",
    "-frames:v", "12", "-an", "-c:v", "mpeg4", "-pix_fmt", "yuv420p", "-threads", "1", $mpeg4)
Add-Fixture "mpeg4" "mpeg4.avi" "video" 12 320 240 "mpeg4" ""
$wav = Join-Path $output "audio.wav"
Generate-Fixture "wav" @("-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=1.2", "-vn", "-c:a", "pcm_s16le", $wav)
Add-Fixture "wav" "audio.wav" "audio" 0 0 0 "" "pcm_s16le"
$aac = Join-Path $output "audio.m4a"
Generate-Fixture "aac" @("-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=1.2", "-vn", "-c:a", "aac", $aac)
Add-Fixture "aac" "audio.m4a" "audio" 0 0 0 "" "aac"
if ($encoders -match '(?m)^\s*[A-Z\.]{6}\s+libmp3lame\s') {
    $mp3 = Join-Path $output "audio.mp3"
    Generate-Fixture "mp3" @("-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=1.2", "-vn", "-c:a", "libmp3lame", $mp3)
    Add-Fixture "mp3" "audio.mp3" "audio" 0 0 0 "" "mp3"
} else { $skipped.Add([ordered]@{ fixture = "mp3"; reason = "external generator has no libmp3lame encoder" }) }
[IO.File]::WriteAllBytes((Join-Path $output "invalid.bin"), [Text.Encoding]::ASCII.GetBytes("Not a media container.`n"))
Add-Fixture "invalid" "invalid.bin" "invalid" 0 0 0 "" ""
# Copy only ftyp from a real MP4: no moov or media packets exist. Unlike ordinary
# truncation (which may remain playable), this boundary cannot describe tracks.
$sourceBytes = [IO.File]::ReadAllBytes($h264)
$ftypLength = ([int]$sourceBytes[0] * 16777216) + ([int]$sourceBytes[1] * 65536) + ([int]$sourceBytes[2] * 256) + [int]$sourceBytes[3]
if ($ftypLength -lt 8 -or $ftypLength -gt $sourceBytes.Length -or [Text.Encoding]::ASCII.GetString($sourceBytes, 4, 4) -ne "ftyp") {
    throw "Generated MP4 does not start with a bounded ftyp atom"
}
$prefix = New-Object byte[] $ftypLength
[Array]::Copy($sourceBytes, $prefix, $ftypLength)
[IO.File]::WriteAllBytes((Join-Path $output "ftyp-only.mp4"), $prefix)
Add-Fixture "severely-truncated" "ftyp-only.mp4" "invalid" 0 0 0 "" ""
$fixtures.ToArray() | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 (Join-Path $output "fixtures.json")

function Run-Acceptance($Fixture, [string]$Mode, [bool]$DirectContext) {
    $scope = if ($DirectContext) { "direct-context" } else { "runtime" }
    $name = "{0}-{1}-{2}" -f $scope, $Fixture.name, $Mode
    $reportPath = Join-Path $output ($name + ".runner.json")
    $arguments = @("--media", $Fixture.media, "--hardware", $Mode, "--report", $reportPath)
    $executable = $hardwareRunner
    if (!$DirectContext) { $arguments += @("--expect", $Fixture.expected); $executable = $runtimeRunner }
    $process = Invoke-BoundedProcess $executable $arguments $name
    Write-Host ("{0}: exit {1}" -f $name, $process.exit_code)
    $failures = New-Object 'System.Collections.Generic.List[string]'
    $report = $null
    try {
        if (!(Test-Path $reportPath -PathType Leaf)) { throw "Runner report missing" }
        $report = Get-Content -Raw $reportPath | ConvertFrom-Json
        if ($report.schema_version -ne 1) { throw "Unexpected runner schema" }
    } catch { $failures.Add($_.Exception.Message) }
    if ($process.exit_code -ne 0 -or $process.killed_by_deadline) { $failures.Add("Process failed or exceeded deadline") }
    $achieved = "not_observed_by_runtime"
    if ($null -ne $report) {
        if ($report.outcome -ne "pass") { $failures.Add("Runner expectation failed") }
        if ($DirectContext) {
            if ($report.evidence_scope -ne "production_hardware_decode_context") { $failures.Add("Wrong direct-context evidence scope") }
            if ($report.decoded_frames -ne $Fixture.expected_frames) { $failures.Add("Decoded frame count differs from generated count") }
            if (!$report.demux_eof -or !$report.decoder_eof) { $failures.Add("Decode did not drain to demux and decoder EOF") }
            if ($report.codec -ne $Fixture.expected_video_codec) { $failures.Add("Decoded codec differs from generated codec") }
            if ($report.hardware_frames + $report.software_frames -ne $Fixture.expected_frames) { $failures.Add("Hardware/software frame accounting differs from generated count") }
            if ($report.transferred_frames -ne $report.hardware_frames) { $failures.Add("Not all actual hardware frames transferred") }
            if ($report.corrupt_packets -ne 0 -or $report.corrupt_frames -ne 0) { $failures.Add("Corrupt packets or frames observed") }
            $dimensions = @($report.decoded_dimensions)
            if ($dimensions.Count -ne 1 -or $dimensions[0].width -ne $Fixture.expected_width -or $dimensions[0].height -ne $Fixture.expected_height) {
                $failures.Add("Decoded dimensions differ from generated dimensions")
            }
            if ($Mode -eq "disabled" -and $report.hardware_frames -ne 0) { $failures.Add("Disabled mode produced hardware frames") }
            $achieved = if ($report.hardware_frames -gt 0) { "hardware_frames_observed" } elseif ($Mode -eq "disabled") { "software_disabled" } else { "software_fallback" }
        } else {
            if ($report.runner -ne "playback_runtime_compatibility") { $failures.Add("Wrong runtime evidence scope") }
            if ($Fixture.expected -ne "invalid") {
                if (!$report.natural_completion -or $report.completion_events -ne 1) { $failures.Add("No unique natural runtime completion") }
                # Runtime callbacks may redraw the same frame (e.g. during an
                # audio tail); exact decoded-frame counts belong to the direct
                # context runner, not to presentation callback counts.
                if ($Fixture.expected_frames -gt 0 -and $report.video_frames -le 0) { $failures.Add("Runtime did not deliver video frames") }
                if ($report.metadata.width -ne $Fixture.expected_width -or $report.metadata.height -ne $Fixture.expected_height) {
                    $failures.Add("Runtime metadata dimensions differ from generated dimensions")
                }
                if ($report.metadata.duration_ms -lt 1000 -or $report.metadata.duration_ms -gt 1600) { $failures.Add("Runtime duration differs from finite fixture duration") }
                $audioTracks = @($report.metadata.tracks | Where-Object { $_.type -eq "audio" })
                $wantedAudio = if ($Fixture.expected_audio_codec) { 1 } else { 0 }
                # MediaInfo.tracks intentionally contains audio/subtitle tracks.
                # Video dimensions and frames prove runtime delivery; codec identity
                # is checked separately by the direct-context decoder below.
                if ($audioTracks.Count -ne $wantedAudio) { $failures.Add("Runtime audio track count differs from generated fixture") }
                if ($wantedAudio -and $audioTracks[0].codec -ne $Fixture.expected_audio_codec) { $failures.Add("Runtime audio codec metadata differs from generated fixture") }
            }
        }
    }
    $result = [ordered]@{
        schema_version = 1; fixture = $Fixture.name; evidence_scope = $scope
        requested_hardware = $Mode; achieved_decode = $achieved
        outcome = if ($failures.Count -eq 0) { "pass" } else { "fail" }
        expected_frames = $Fixture.expected_frames; expected_width = $Fixture.expected_width; expected_height = $Fixture.expected_height
        process = $process; runner_report_path = $reportPath; runner = $report; failures = @($failures.ToArray())
    }
    $result | ConvertTo-Json -Depth 20 | Set-Content -Encoding UTF8 (Join-Path $output ($name + ".json"))
    $results.Add($result)
}

$oldAudioDriver = $env:SDL_AUDIODRIVER
$oldVideoDriver = $env:SDL_VIDEODRIVER
try {
    # Exercise actual runtime timing/decoder/render callbacks without requiring speakers or a GUI.
    $env:SDL_AUDIODRIVER = "dummy"
    $env:SDL_VIDEODRIVER = "dummy"
    foreach ($fixture in $fixtures) {
        $modes = if ($fixture.expected -eq "video" -or $fixture.expected -eq "av") { @("disabled", "auto", "d3d11va") } else { @("disabled") }
        foreach ($mode in $modes) { Run-Acceptance $fixture $mode $false }
    }
    foreach ($fixture in @($fixtures | Where-Object { $_.expected_video_codec })) {
        foreach ($mode in @("disabled", "auto", "d3d11va")) { Run-Acceptance $fixture $mode $true }
    }
} finally {
    $env:SDL_AUDIODRIVER = $oldAudioDriver
    $env:SDL_VIDEODRIVER = $oldVideoDriver
}

$failures = @($results | Where-Object { $_.outcome -ne "pass" })
$hardwareRows = @($results | Where-Object { $_.evidence_scope -eq "direct-context" -and $_.achieved_decode -eq "hardware_frames_observed" })
$fallbackRows = @($results | Where-Object { $_.evidence_scope -eq "direct-context" -and $_.achieved_decode -eq "software_fallback" })
$summary = [ordered]@{
    schema_version = 1; acceptance = "media_compatibility_and_decode_fallback"
    outcome = if ($failures.Count -eq 0) { "pass" } else { "fail" }
    build_directory = $build; configuration = $Configuration
    runtime_executable = $runtimeRunner; hardware_executable = $hardwareRunner
    external_ffmpeg_executable = $ffmpegPath
    # Get-Content strings carry PSDrive/PSProvider note properties in PS5.1;
    # serializing them deeply walks the provider graph instead of plain text.
    external_ffmpeg_version = [IO.File]::ReadAllText($generatorVersion.stdout_log)
    gpu_inventory_not_adapter_proof = $gpuInventory; gpu_inventory_error = $gpuInventoryError
    hardware_frame_cases = $hardwareRows.Count; successful_software_fallback_cases = @($fallbackRows | Where-Object { $_.outcome -eq "pass" }).Count
    scope = "Runtime cases exercise production playback with dummy SDL sinks, real metadata/frame callbacks and timed natural EOF. Hardware cases separately exercise production HardwareDecodeContext directly, not GUI/runtime adapter use."
    limitations = @(
        "GPU inventory is not evidence of selected adapter. Only direct-context adapter observations tied to actual hardware frames identify decoder use.",
        "Software fallback is valid acceptance, not hardware coverage. Inspect each fallback_diagnostic to distinguish unsupported codec from unavailable device; unobserved device-failure branches remain unproven.",
        "Audio evidence is track metadata, Playing and timed EOF; no acoustic output capture or sample-fidelity/A-V-sync claim.",
        "Only ftyp-only MP4 is deliberately rejected: other truncated media may legitimately remain playable.",
        "This finite matrix does not establish all GPUs/drivers/codecs, full GUI presentation, benchmarks, or release readiness."
    )
    skipped = @($skipped.ToArray()); fixtures = @($fixtures.ToArray()); processes = @($processes.ToArray()); results = @($results.ToArray())
}
$summaryPath = Join-Path $output "compatibility.json"
$summary | ConvertTo-Json -Depth 24 | Set-Content -Encoding UTF8 $summaryPath
Write-Host "Compatibility evidence: $summaryPath"
Write-Host ("Actual hardware-frame cases: {0}; software-fallback cases: {1}; failures: {2}" -f $hardwareRows.Count, $fallbackRows.Count, $failures.Count)
if ($failures.Count -gt 0) { throw "Media compatibility acceptance failed in $($failures.Count) cases; inspect compatibility.json and per-case logs" }
