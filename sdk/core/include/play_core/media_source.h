#pragma once
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace play_core {
struct Rational { int num{1}; int den{1}; };
enum class RtspTransport { Tcp, Udp };
struct NetworkOptions {
    std::chrono::milliseconds connectTimeout{10000};
    std::chrono::milliseconds readTimeout{15000};
    std::chrono::microseconds analyzeDuration{5000000};
    std::int64_t probeSize{5 * 1024 * 1024};
    RtspTransport rtspTransport{RtspTransport::Tcp};
    std::string userAgent{"PlayerSDK/1.0"};
    std::map<std::string, std::string> headers;
    int maxReconnectAttempts{5};
    std::chrono::milliseconds initialReconnectDelay{1000};
    std::chrono::milliseconds maxReconnectDelay{15000};
    std::chrono::seconds stablePlaybackReset{30};
    bool reconnect{true};
};
// File paths are UTF-8 native paths (not percent-encoded file:// URLs).
// Network sources use a scheme:// URL. Input is owned by the request.
struct MediaSource { std::string location; NetworkOptions network; bool enableAudio{true}; };
enum class MediaType { Unknown, Video, Audio, Subtitle };
struct TrackInfo {
    int streamIndex{-1};
    MediaType type{MediaType::Unknown};
    std::string language, title, codec;
    bool isDefault{false}, isForced{false};
};
struct MediaInfo {
    bool networkSource{false}, live{false}, seekable{false};
    std::optional<std::chrono::milliseconds> duration;
    std::vector<TrackInfo> tracks;
    int width{0}, height{0};
    Rational sampleAspectRatio;
    double rotationDegrees{0};
};
}
