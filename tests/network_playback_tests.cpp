#include "network_input.h"

#include <chrono>
#include <iostream>

namespace {

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

} // namespace

int main()
{
    using namespace std::chrono_literals;

    if (!Expect(ClassifyMediaSource("movie.mp4") == MediaSourceKind::LocalFile, "local file"))
        return 1;
    if (!Expect(ClassifyMediaSource("https://host/live.m3u8") == MediaSourceKind::Http, "https"))
        return 1;
    if (!Expect(ClassifyMediaSource("rtsp://host/live") == MediaSourceKind::Rtsp, "rtsp"))
        return 1;
    if (!Expect(IsRealtimeSource(MediaSourceKind::Rtp), "rtp is realtime"))
        return 1;
    if (!Expect(!ValidateMediaSource(MediaSource{}).ok, "empty source rejected"))
        return 1;

    NetworkOptions options;
    if (!Expect(ReconnectDelay(options, 1) == 1s, "first delay"))
        return 1;
    if (!Expect(ReconnectDelay(options, 5) == 15s, "delay capped"))
        return 1;
    if (!Expect(ShouldReconnect(PlaybackError::Timeout), "timeout retries"))
        return 1;
    if (!Expect(!ShouldReconnect(PlaybackError::Authentication), "auth does not retry"))
        return 1;
    if (!Expect(RedactMediaLocation("https://u:p@h/x?token=secret") == "https://***:***@h/x?token=***",
            "credentials redacted"))
        return 1;

    return 0;
}
