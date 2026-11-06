#pragma once

#include "media_source.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <utility>

class ReconnectController final {
public:
    ReconnectController() = default;

    void reset();
    void cancel();
    [[nodiscard]] bool cancelled() const noexcept;
    [[nodiscard]] bool wait(std::chrono::milliseconds delay);
    [[nodiscard]] bool canRetry(const MediaSource& source,
                                PlaybackError error,
                                int completedAttempts,
                                bool streamAborted) const;
    template <typename Func>
    [[nodiscard]] bool runIfNotCancelled(Func&& func)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_cancelled.load(std::memory_order_acquire))
            return false;
        std::forward<Func>(func)();
        return true;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::atomic_bool m_cancelled{true};
};
