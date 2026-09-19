#include "playback_controller.h"

#include <utility>

PlaybackController::PlaybackController(Actions actions)
    : m_actions(std::move(actions))
{
}

bool PlaybackController::play(const MediaSource& source)
{
    return m_actions.play ? m_actions.play(source) : false;
}

bool PlaybackController::play(const std::string& location)
{
    return play(MediaSource{location});
}

void PlaybackController::pause()
{
    if (m_actions.pause)
        m_actions.pause();
}

void PlaybackController::seek(double percent)
{
    if (m_actions.seek)
        m_actions.seek(percent);
}

void PlaybackController::seekSeconds(int seconds)
{
    if (m_actions.seekSeconds)
        m_actions.seekSeconds(seconds);
}

void PlaybackController::seekForward()
{
    if (m_actions.seekForward)
        m_actions.seekForward();
}

void PlaybackController::seekBack()
{
    if (m_actions.seekBack)
        m_actions.seekBack();
}

void PlaybackController::stop()
{
    if (m_actions.stop)
        m_actions.stop();
}

void PlaybackController::stopAndWait()
{
    if (m_actions.stopAndWait)
        m_actions.stopAndWait();
}

void PlaybackController::setVolume(double percent)
{
    if (m_actions.setVolume)
        m_actions.setVolume(percent);
}

void PlaybackController::setSpeed(double speed)
{
    if (m_actions.setSpeed)
        m_actions.setSpeed(speed);
}

void PlaybackController::setLoopPolicy(VideoLoopPolicy policy)
{
    if (m_actions.setLoopPolicy)
        m_actions.setLoopPolicy(policy);
}

void PlaybackController::cycleAudioTrack()
{
    if (m_actions.cycleAudioTrack)
        m_actions.cycleAudioTrack();
}

void PlaybackController::cycleSubtitleTrack()
{
    if (m_actions.cycleSubtitleTrack)
        m_actions.cycleSubtitleTrack();
}

void PlaybackController::setHardwareDecodePreference(HardwareDecodePreference preference)
{
    if (m_actions.setHardwareDecodePreference)
        m_actions.setHardwareDecodePreference(preference);
}

void PlaybackController::selectAudioTrack(int streamIndex)
{
    if (m_actions.selectAudioTrack)
        m_actions.selectAudioTrack(streamIndex);
}

void PlaybackController::selectSubtitleTrack(std::optional<int> streamIndex)
{
    if (m_actions.selectSubtitleTrack)
        m_actions.selectSubtitleTrack(streamIndex);
}

void PlaybackController::addVolume()
{
    if (m_actions.addVolume)
        m_actions.addVolume();
}

void PlaybackController::subVolume()
{
    if (m_actions.subVolume)
        m_actions.subVolume();
}
