#include "playback_command_mailbox.h"

void PlaybackCommandMailbox::postPauseToggle()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_pending.pauseToggleCount;
}

void PlaybackCommandMailbox::postSeek(SeekCommand command)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pending.seek = command;
}

void PlaybackCommandMailbox::postTrack(TrackCommand command)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pending.tracks.push_back(std::move(command));
}

PlaybackCommands PlaybackCommandMailbox::take()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    PlaybackCommands commands = std::move(m_pending);
    m_pending = {};
    return commands;
}

void PlaybackCommandMailbox::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pending = {};
}
