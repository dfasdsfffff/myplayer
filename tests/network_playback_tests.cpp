#include "network_input.h"

#include <cerrno>
#include <chrono>
#include <iostream>
#include <thread>

extern "C" {
#include <libavformat/avformat.h>
}

namespace {

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

std::string DictionaryValue(AVDictionary* dictionary, const char* key)
{
    AVDictionaryEntry* entry = av_dict_get(dictionary, key, nullptr, 0);
    return entry ? entry->value : "";
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

    MediaSource rtsp{"rtsp://host/live"};
    rtsp.network.rtspTransport = RtspTransport::Udp;
    AvDictionary rtspOptions = BuildInputOptions(rtsp);
    if (!Expect(DictionaryValue(rtspOptions.get(), "rtsp_transport") == "udp", "RTSP transport"))
        return 1;
    if (!Expect(DictionaryValue(rtspOptions.get(), "rw_timeout") == "15000000", "read timeout"))
        return 1;

    MediaSource http{"https://host/vod.mp4"};
    http.network.headers["Authorization"] = "Bearer secret";
    AvDictionary httpOptions = BuildInputOptions(http);
    if (!Expect(DictionaryValue(httpOptions.get(), "reconnect") == "1", "HTTP reconnect"))
        return 1;
    if (!Expect(DictionaryValue(httpOptions.get(), "reconnect_on_http_error") == "500,502,503,504",
            "HTTP retry statuses"))
        return 1;

    if (!Expect(MapAvError(AVERROR(ETIMEDOUT), false) == PlaybackError::Timeout, "timeout error"))
        return 1;
    if (!Expect(MapAvError(AVERROR_HTTP_UNAUTHORIZED, false) == PlaybackError::Authentication, "auth error"))
        return 1;
    if (!Expect(MapAvError(AVERROR_EOF, true) == PlaybackError::ConnectionLost, "realtime eof"))
        return 1;

    IoControl io;
    io.begin(IoOperation::Opening, 1ms);
    std::this_thread::sleep_for(3ms);
    if (!Expect(InterruptNetworkIo(&io) != 0, "expired deadline interrupts"))
        return 1;
    io.end();
    if (!Expect(InterruptNetworkIo(&io) == 0, "cleared deadline does not interrupt"))
        return 1;
    io.cancelled = true;
    if (!Expect(InterruptNetworkIo(&io) != 0, "cancel interrupts"))
        return 1;

    if (!Expect(BuildMediaInfo(MediaSource{"udp://host:9000"}, nullptr).live, "UDP is live"))
        return 1;
    if (!Expect(!BuildMediaInfo(MediaSource{"udp://host:9000"}, nullptr).seekable, "live not seekable"))
        return 1;
    if (!Expect(!CanSeek(MediaInfo{true, true, false, std::nullopt}), "seek rejected"))
        return 1;
    if (!Expect(UseUnlimitedBuffer(MediaSource{"rtp://host:9000"}, false), "realtime uses unlimited buffer"))
        return 1;
    if (!Expect(!UseUnlimitedBuffer(MediaSource{"movie.mp4"}, false), "local uses normal buffer"))
        return 1;

    return 0;
}
