#include "reconnect_controller.h"

#include "network_input.h"

void ReconnectController::reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cancelled.store(false, std::memory_order_release);
}

void ReconnectController::cancel()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cancelled.store(true, std::memory_order_release);
    }
    m_condition.notify_all();
}

bool ReconnectController::cancelled() const noexcept
{
    return m_cancelled.load(std::memory_order_acquire);
}

bool ReconnectController::wait(std::chrono::milliseconds delay)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_cancelled.load(std::memory_order_acquire))
        return false;

    return !m_condition.wait_for(lock, delay, [this] {
        return m_cancelled.load(std::memory_order_acquire);
    });
}

bool ReconnectController::canRetry(const MediaSource& source,
                                   PlaybackError error,
                                   int completedAttempts,
                                   bool streamAborted) const
{
    return source.network.reconnect
        && ShouldReconnect(error)
        && completedAttempts < source.network.maxReconnectAttempts
        && !streamAborted
        && !cancelled();
}
