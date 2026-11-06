#include "renderer_dispatcher.h"

#include <utility>

void RendererDispatcher::setFrameDimensionsChangedCallback(FrameDimensionsChangedCallback callback)
{
    m_frameDimensionsChangedCallback = std::move(callback);
}

void RendererDispatcher::setVideoFrameCallback(VideoFrameCallback callback)
{
    m_videoFrameCallback = std::move(callback);
}

void RendererDispatcher::dispatchFrame(std::shared_ptr<VideoFrame> frame)
{
    if (!frame || frame->width <= 0 || frame->height <= 0)
        return;

    if (m_frameWidth != frame->width || m_frameHeight != frame->height) {
        m_frameWidth = frame->width;
        m_frameHeight = frame->height;
        if (m_frameDimensionsChangedCallback)
            m_frameDimensionsChangedCallback(m_frameWidth, m_frameHeight);
    }

    if (m_videoFrameCallback)
        m_videoFrameCallback(std::move(frame));
}

int RendererDispatcher::frameWidth() const noexcept
{
    return m_frameWidth;
}

int RendererDispatcher::frameHeight() const noexcept
{
    return m_frameHeight;
}
