#pragma once

#include "media_source.h"
#include "packet_queue.h"

#include <functional>

struct AVFormatContext;
struct AVStream;
struct VideoState;

struct StreamReaderCallbacks {
    std::function<int(VideoState*, int)> openComponent;
    std::function<void(const PlaybackStatus&)> publishStatus;
    std::function<void(int)> publishDurationSeconds;
    std::function<void(const MediaInfo&)> publishMediaInfo;
    std::function<void()> stopRefreshLoop;
    std::function<void()> handleEndOfMedia;
};

class StreamReader final {
public:
    explicit StreamReader(StreamReaderCallbacks callbacks);

    void Run(VideoState* state);

    static bool HasEnoughPackets(AVStream* stream, int streamId, PacketQueue* queue);
    static bool IsRealtime(const AVFormatContext* context);

private:
    StreamReaderCallbacks m_callbacks;
};
