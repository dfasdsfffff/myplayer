#include "playback_runtime.h"

#include "playback_controller_videoctl.h"
#include "videoctl.h"

std::unique_ptr<PlaybackRuntime> PlaybackRuntime::Create()
{
    auto videoCtl = VideoCtl::MakeInstance();
    if (!videoCtl)
        return nullptr;
    return std::unique_ptr<PlaybackRuntime>(new PlaybackRuntime(std::move(videoCtl)));
}

PlaybackRuntime::PlaybackRuntime(std::shared_ptr<VideoCtl> videoCtl)
    : m_videoCtl(std::move(videoCtl)),
      m_controller(CreatePlaybackController(*m_videoCtl))
{
    connectSignals();
}

PlaybackRuntime::~PlaybackRuntime()
{
    m_controller.stopAndWait();
    m_connections.clear();
}

PlaybackController& PlaybackRuntime::controller()
{
    return m_controller;
}

const PlaybackController& PlaybackRuntime::controller() const
{
    return m_controller;
}

void PlaybackRuntime::connectSignals()
{
    m_connections.emplace_back(m_videoCtl->SigPlayMsg.connect([this](const std::string& msg) {
        SigPlayMsg(msg);
    }));
    m_connections.emplace_back(m_videoCtl->SigFrameDimensionsChanged.connect([this](int width, int height) {
        SigFrameDimensionsChanged(width, height);
    }));
    m_connections.emplace_back(m_videoCtl->SigVideoFrame.connect([this](std::shared_ptr<VideoFrame> frame) {
        SigVideoFrame(std::move(frame));
    }));
    m_connections.emplace_back(m_videoCtl->SigVideoTotalSeconds.connect([this](int seconds) {
        SigVideoTotalSeconds(seconds);
    }));
    m_connections.emplace_back(m_videoCtl->SigVideoPlaySeconds.connect([this](int seconds) {
        SigVideoPlaySeconds(seconds);
    }));
    m_connections.emplace_back(m_videoCtl->SigVideoVolume.connect([this](double volume) {
        SigVideoVolume(volume);
    }));
    m_connections.emplace_back(m_videoCtl->SigPauseStat.connect([this](bool paused) {
        SigPauseStat(paused);
    }));
    m_connections.emplace_back(m_videoCtl->SigStop.connect([this]() {
        SigStop();
    }));
    m_connections.emplace_back(m_videoCtl->SigStopFinished.connect([this]() {
        SigStopFinished();
    }));
    m_connections.emplace_back(m_videoCtl->SigStartPlay.connect([this](const std::string& fileName) {
        SigStartPlay(fileName);
    }));
    m_connections.emplace_back(m_videoCtl->SigPlayNextOne.connect([this]() {
        SigPlayNextOne();
    }));
    m_connections.emplace_back(m_videoCtl->SigRandomPlayOne.connect([this]() {
        SigRandomPlayOne();
    }));
}
