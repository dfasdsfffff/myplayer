#pragma once

#include <functional>

struct VideoState;

class MediaSession {
public:
    using CloseCallback = std::function<void(VideoState*)>;

    MediaSession() = default;
    MediaSession(VideoState* state, CloseCallback closeCallback);
    ~MediaSession();

    MediaSession(const MediaSession&) = delete;
    MediaSession& operator=(const MediaSession&) = delete;

    MediaSession(MediaSession&& other) noexcept;
    MediaSession& operator=(MediaSession&& other) noexcept;

    VideoState* get() const noexcept;
    explicit operator bool() const noexcept;

    void reset(VideoState* state = nullptr);
    VideoState* release() noexcept;
    void setCloseCallback(CloseCallback closeCallback);

private:
    VideoState* m_state = nullptr;
    CloseCallback m_closeCallback;
};
