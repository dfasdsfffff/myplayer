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
