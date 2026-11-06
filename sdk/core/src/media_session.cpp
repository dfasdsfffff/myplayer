#include "media_session.h"

#include <utility>

MediaSession::MediaSession(VideoState* state, CloseCallback closeCallback)
    : m_state(state),
      m_closeCallback(std::move(closeCallback))
{
}

MediaSession::~MediaSession()
{
    reset();
}

MediaSession::MediaSession(MediaSession&& other) noexcept
    : m_state(other.release()),
      m_closeCallback(std::move(other.m_closeCallback))
{
}

MediaSession& MediaSession::operator=(MediaSession&& other) noexcept
{
    if (this != &other) {
        reset();
        m_state = other.release();
        m_closeCallback = std::move(other.m_closeCallback);
    }
    return *this;
}

VideoState* MediaSession::get() const noexcept
{
    return m_state;
}

MediaSession::operator bool() const noexcept
{
    return m_state != nullptr;
}

void MediaSession::reset(VideoState* state)
{
    if (m_state && m_closeCallback)
        m_closeCallback(m_state);
    m_state = state;
}

VideoState* MediaSession::release() noexcept
{
    VideoState* state = m_state;
    m_state = nullptr;
    return state;
}

void MediaSession::setCloseCallback(CloseCallback closeCallback)
{
    m_closeCallback = std::move(closeCallback);
}
