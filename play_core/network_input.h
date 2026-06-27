#pragma once

#include "media_source.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

extern "C" {
#include <libavutil/dict.h>
}

struct AVFormatContext;

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

class AvDictionary {
public:
    AvDictionary() = default;
    ~AvDictionary();

    AvDictionary(const AvDictionary&) = delete;
    AvDictionary& operator=(const AvDictionary&) = delete;

    AvDictionary(AvDictionary&& other) noexcept;
    AvDictionary& operator=(AvDictionary&& other) noexcept;

    AVDictionary* get() const noexcept;
    AVDictionary** put() noexcept;

private:
    AVDictionary* m_dictionary = nullptr;
};

enum class IoOperation {
    None,
    Opening,
    Probing,
    Reading
};

struct IoControl {
    std::atomic_bool cancelled{false};
    std::atomic<std::int64_t> deadlineUs{0};
    std::atomic<IoOperation> operation{IoOperation::None};

    void begin(IoOperation operation, std::chrono::milliseconds timeout);
    void end();
};

AvDictionary BuildInputOptions(const MediaSource& source);
PlaybackError MapAvError(int avError, bool realtime);
int InterruptNetworkIo(void* opaque);
MediaInfo BuildMediaInfo(const MediaSource& source, AVFormatContext* formatContext);
bool CanSeek(const MediaInfo& info);
bool UseUnlimitedBuffer(const MediaSource& source, bool formatRealtime);
