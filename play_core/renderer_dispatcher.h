#pragma once

#include "video_frame.h"

#include <functional>
#include <memory>

class RendererDispatcher {
public:
    using FrameDimensionsChangedCallback = std::function<void(int, int)>;
    using VideoFrameCallback = std::function<void(std::shared_ptr<VideoFrame>)>;

    void setFrameDimensionsChangedCallback(FrameDimensionsChangedCallback callback);
    void setVideoFrameCallback(VideoFrameCallback callback);

    void dispatchFrame(std::shared_ptr<VideoFrame> frame);

    int frameWidth() const noexcept;
    int frameHeight() const noexcept;

private:
    FrameDimensionsChangedCallback m_frameDimensionsChangedCallback;
    VideoFrameCallback m_videoFrameCallback;
    int m_frameWidth = 0;
    int m_frameHeight = 0;
};
