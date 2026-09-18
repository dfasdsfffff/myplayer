#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/rational.h>
}

enum class RtspTransport {
    Tcp,
    Udp
};

struct NetworkOptions {
    std::chrono::milliseconds connectTimeout{10000};
    std::chrono::milliseconds readTimeout{15000};
    std::chrono::microseconds analyzeDuration{5000000};
    std::int64_t probeSize{5 * 1024 * 1024};

    RtspTransport rtspTransport{RtspTransport::Tcp};
    std::string userAgent{"myplayer/1.0"};
    std::map<std::string, std::string> headers;

    int maxReconnectAttempts{5};
    std::chrono::milliseconds initialReconnectDelay{1000};
    std::chrono::milliseconds maxReconnectDelay{15000};
    std::chrono::seconds stablePlaybackReset{30};
    bool reconnect{true};
};

struct MediaSource {
    std::string location;
    NetworkOptions network;
};

enum class PlaybackState {
    Opening,
    Buffering,
    Reconnecting,
    Playing,
    Failed,
    Stopped
};

enum class PlaybackError {
    None,
    Cancelled,
    Timeout,
    Authentication,
    NotFound,
    UnsupportedProtocol,
    NetworkUnavailable,
    ConnectionLost,
    InvalidMedia,
    DecoderFailure,
    Unknown
};

struct PlaybackStatus {
    PlaybackState state{PlaybackState::Stopped};
    PlaybackError error{PlaybackError::None};
    int reconnectAttempt{0};
    int maxReconnectAttempts{0};
    std::chrono::milliseconds retryAfter{0};
    std::string message;
};

struct TrackInfo {
    int streamIndex{-1};
    AVMediaType type{AVMEDIA_TYPE_UNKNOWN};
    std::string language;
    std::string title;
    std::string codec;
    bool isDefault{false};
    bool isForced{false};
};

struct MediaInfo {
    bool networkSource{false};
    bool live{false};
    bool seekable{false};
    std::optional<std::chrono::milliseconds> duration;
    std::vector<TrackInfo> tracks;
    int width{0};
    int height{0};
    AVRational sampleAspectRatio{1, 1};
    double rotationDegrees{0.0};
};
