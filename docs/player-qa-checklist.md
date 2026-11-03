# MyPlayer Player QA Checklist

Run this checklist before calling a build commercial-ready.

## format compatibility

- MP4 with H.264/AAC.
- MKV with H.264 or H.265.
- AVI with legacy codecs.
- FLV.
- WMV.
- MP3.
- AAC.
- Files with no audio track.
- Files with no video track.
- Corrupt or truncated media files.
- Very short files under 5 seconds.
- Long files over 2 hours.

### Automated Compatibility And Decode Acceptance

`media_compatibility_tests` uses the production runtime. Its no-argument CTest
cases generate MPEG4 video, PCM WAV, invalid bytes and an unusable AVI prefix;
they verify finite playback/error boundaries, prevent premature audio EOF and
duplicate next-item events, and check that single-item repeat still seeks and
remains stoppable across EOF.

`scripts/verify-media-compatibility.ps1` generates a broader finite matrix using
an external FFmpeg encoder. It checks runtime playback separately from direct
decoding with production `HardwareDecodeContext`; see the README for commands.
MediaInfo's track list contains audio/subtitle tracks, not video tracks.
Runtime video evidence uses valid frame storage and dimensions; exact counts
are checked only by the direct decoder, since runtime callbacks may redraw.

#### Compatibility Verification — 2026-11-03

- Release matrix: 23/23 cases passed, with no skipped fixtures.
- Runtime: H.264/AAC MP4, Main8 H.265 MKV and MPEG4 AVI in disabled/Auto/D3D11VA
  modes; WAV, AAC and MP3 audio-only; invalid bytes and ftyp-only MP4 rejection.
- Direct context: each video decoded all 12 expected frames in all three modes
  and drained both demuxer and decoder; frames had valid dimensions/storage.
- Four cases produced 12 D3D11 frames each and transferred all 12 to CPU frames:
  H.264 and H.265, each in Auto and D3D11VA modes.
- The adapter queried from those hardware frames was Intel(R) Arc(TM) Graphics,
  driver 32.0.101.6078 from the matching local inventory. The installed NVIDIA
  RTX 4060 Laptop GPU was not selected and is not covered by this run.
- Two MPEG4 cases (Auto/D3D11VA) actually fell back to software after pixel-format
  negotiation. Device-creation failure/unavailable-device recovery was not induced.
- WAV/AAC/MP3 fixtures of 1200 ms reached natural EOF at approximately 1336–1343 ms.
  This verifies output-queue timing, not acoustic fidelity or synchronization.
- Fixed short-audio EOF to wait for in-flight PCM production, queued PCM and
  the output-buffer tail. Fixed repeated EOF notifications; a successful seek
  re-arms the notification for single-item repeat.
- Twelve affected Release tests passed. Compatibility and audio-ring tests
  each passed five consecutive runs.
- A separate real Qt/SDL window rendered a repeated HEVC test pattern for a
  bounded five-second run (software decode), submitted 50 frames and exited 0.
- Linked runtime FFmpeg was 9.0.1; fixture-generator FFmpeg was 7.1.1.

Evidence is under `.cache/media-compatibility/20261103-155824-c6f25b2a266449c2ab2c3aed3929c9ea/`;
the desktop screenshot/report are `.cache/media-gui-smoke.png` and
`.cache/media-gui-smoke.json`. This is one adapter/profile sample, not the full
hardware publication gate. FLV/WMV, other profiles/adapters, two-hour media,
eight-hour stability, and audio/video-sync acceptance remain outstanding.

## network failure

- HTTP video URL.
- HTTPS video URL.
- HLS playlist URL.
- RTSP over TCP.
- RTSP over UDP.
- UDP/RTP live stream.
- DNS failure.
- Connection timeout.
- Server disconnect during playback.
- Authentication failure URL.
- URL containing credentials; verify logs and playlist display redact sensitive data where applicable.

### Automated HTTP Runtime Acceptance

`network_integration_tests` runs the actual playback runtime against a bounded,
loopback-only HTTP server with generated MPEG-TS video. It verifies:

- HTTP 404 and 401 are categorized correctly and do not retry, even when reconnect is enabled.
- An opening deadline produces Timeout rather than Cancelled.
- Explicit stop interrupts both an unanswered request and an active stream,
  completes exactly one session, and does not publish a cancellation failure.
- Server reset after video delivery is reported as a network failure.
- A stalled body read after video delivery produces Timeout, including when
  the demuxer returns buffered packets before exposing the underlying AVIO error.
- Stop interrupts a ten-second reconnect backoff in under 1500 ms and publishes
  Stopped plus exactly one session completion.
- A complete finite HTTP video reaches natural end-of-media without a failure.

Build `network_integration_tests`, then run:

```powershell
ctest --test-dir build -C Release -R "^network_integration_tests$" --output-on-failure
```

CTest supplies dummy SDL drivers and a 60-second process timeout. Fixtures and
server sockets are isolated per invocation and cleaned up on exit.
This coverage does not establish HTTPS, HLS, RTSP, UDP/RTP, DNS-failure, or
eight-hour network-stream acceptance.

#### Network Verification — 2026-11-03

- The nine affected Release tests passed: application smoke, playback integration,
  lifecycle stress, network integration, network policy, stream reader,
  reconnect controller, video widget, and main-window behavior.
- All nine HTTP integration scenarios passed five consecutive runs.
- A separate actual GUI run used the Open Link dialog and a loopback H.264
  MPEG-TS stream; a screenshot confirmed video and the Playing status.
- GUI close completed in 234 ms during streaming and 128 ms while waiting
  for HTTP response headers; both player processes exited 0.
- Fixes preserve timeout causes across buffered demuxer output, retain AVIO
  socket errors, report read failures instead of discarding them, avoid
  failure status for user cancellation, and complete cancelled reconnects.

## long-running playback

- Play a local file for at least 8 hours.
- Play a network stream for at least 8 hours.
- Monitor memory growth.
- Monitor handle count.
- Monitor CPU usage.
- Confirm no progressive audio/video drift.

## Seeking And Controls

- Drag progress while playing.
- Drag progress while paused.
- Seek near the beginning.
- Seek near the end.
- Switch playback speed.
- Change volume repeatedly.
- Toggle pause and resume rapidly.
- Stop during network buffering.
- Close the app while decoding.

### Automated Runtime Transition Acceptance

`playback_integration_tests` generates isolated local fixtures and verifies:

- Opening precedes Playing, with metadata and a fresh video frame.
- Pause acknowledgement and frame quiescence; three resume cycles deliver new frames.
- Paused forward seek and rewind reach distinct decoded image regions without resuming.
- Explicit stop joins exactly one session and leaves no subsequent frame delivery.
- Replay after stop, including immediate stop → replay ordering.
- Runtime destruction during playback completes the active session.
- Audio-only WAV metadata, Playing status, and shutdown completion.

Run after building the target:

```powershell
ctest --test-dir build -C Release -R "^playback_integration_tests$" --output-on-failure
```

This is runtime-level acceptance using dummy SDL drivers, not GUI rendering
or real audio-device validation. Seek checks image regions, not exact timestamps.
The 60-second CTest timeout also bounds synchronous shutdown deadlocks.

## GUI Performance Baseline

Run `scripts/benchmark-video-presentation.ps1` on an interactive desktop; see
the README for arguments and metric definitions. Preserve `baseline.json`,
player reports, process samples, configuration, and hardware/display details
when comparing changes. Avoid other heavy workloads during measurement.

### Local Verification — 2026-11-03

- MSVC Release build passed using Qt 6.9.3 and FFmpeg libavcodec 63.1.101.
- Release CTest passed: application smoke, playback integration, network integration,
  video widget, and main-window behavior (5/5).
- Actual Qt/SDL playback completed both generated fixtures with a 3000 ms run
  request, software decode, and requested 1280×720 window. Both processes exited 0.
- A native-window screenshot showed the 1080p test pattern during playback;
  a separate run reached natural end-of-media and completed teardown.
- Invalid arguments exited 2; readable non-media input exited 1; a forced
  2 ms watchdog deadline exited 3 with `hard_timeout` and incomplete teardown.
- This short local run does not clear the eight-hour playback, clean-machine,
  hardware-decoding, real-network/protocol-matrix, or audio/video-sync acceptance items.

## Playlist And Files

- Drag local files into the player.
- Drag folders where supported.
- Add network URL through the open link dialog.
- Import M3U/M3U8 playlist.
- Export playlist containing both local files and network streams.
- Remove current item while playing.
- Clear playlist while stopped.

## Crash Logs And Diagnostics

- Confirm logs include playback failures with useful error categories.
- Confirm logs do not expose credentials or tokens.
- Confirm crash dumps or OS crash reports can identify the executable version.
- Confirm user-facing errors are actionable and do not show raw internal-only messages.
