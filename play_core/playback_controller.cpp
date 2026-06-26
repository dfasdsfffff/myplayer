#include "playback_controller.h"

#include <utility>

PlaybackController::PlaybackController(Actions actions)
    : m_actions(std::move(actions))
{
}

bool PlaybackController::play(const std::string& fileName)
{
    return m_actions.play ? m_actions.play(fileName) : false;
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
