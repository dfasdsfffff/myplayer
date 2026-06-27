#pragma once

#include "media_source.h"

#include <chrono>
#include <string>
#include <string_view>

enum class MediaSourceKind {
    LocalFile,
    Http,
    Rtsp,
    Rtp,
    Udp,
    OtherNetwork
};

struct ValidationResult {
    bool ok{false};
    std::string message;
};

MediaSourceKind ClassifyMediaSource(std::string_view location);
bool IsNetworkSource(MediaSourceKind kind);
bool IsRealtimeSource(MediaSourceKind kind);
ValidationResult ValidateMediaSource(const MediaSource& source);
std::chrono::milliseconds ReconnectDelay(const NetworkOptions& options, int attempt);
bool ShouldReconnect(PlaybackError error);
std::string RedactMediaLocation(std::string_view location);
