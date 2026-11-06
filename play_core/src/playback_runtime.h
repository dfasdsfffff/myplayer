#pragma once

#include "playback_controller.h"
#include "signal.h"
#include "subtitle_frame.h"
#include "video_frame.h"

#include <memory>
#include <string>
#include <vector>

class VideoCtl;

class PlaybackRuntime {
public:
    static std::unique_ptr<PlaybackRuntime> Create();

    PlaybackRuntime(const PlaybackRuntime&) = delete;
    PlaybackRuntime& operator=(const PlaybackRuntime&) = delete;
    ~PlaybackRuntime();

    PlaybackController& controller();
    const PlaybackController& controller() const;

    Signal<const std::string&> SigPlayMsg;
    Signal<int, int> SigFrameDimensionsChanged;
    Signal<std::shared_ptr<VideoFrame>> SigVideoFrame;
    Signal<std::shared_ptr<const SubtitleFrame>> SigSubtitleFrame;
    Signal<int> SigVideoTotalSeconds;
    Signal<int> SigVideoPlaySeconds;
    Signal<double> SigVideoVolume;
    Signal<bool> SigPauseStat;
    Signal<> SigStop;
    Signal<> SigStopFinished;
    Signal<const std::string&> SigStartPlay;
    Signal<const PlaybackStatus&> SigPlaybackStatus;
    Signal<const MediaInfo&> SigMediaInfo;
    Signal<> SigPlayNextOne;
    Signal<> SigRandomPlayOne;

private:
    explicit PlaybackRuntime(std::shared_ptr<VideoCtl> videoCtl);

    void connectSignals();

    std::shared_ptr<VideoCtl> m_videoCtl;
    PlaybackController m_controller;
    std::vector<sigslot::scoped_connection> m_connections;
};
