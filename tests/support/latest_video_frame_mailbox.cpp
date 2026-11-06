#include "latest_video_frame_mailbox.h"

void LatestVideoFrameMailbox::publish(std::shared_ptr<VideoFrame> frame)
{
    if (m_latestFrame.exchange(std::move(frame), std::memory_order_acq_rel))
        m_coalescedFrames.fetch_add(1, std::memory_order_relaxed);
}

std::shared_ptr<VideoFrame> LatestVideoFrameMailbox::takeLatest()
{
    return m_latestFrame.exchange({}, std::memory_order_acq_rel);
}

bool LatestVideoFrameMailbox::hasPending() const
{
    return static_cast<bool>(m_latestFrame.load(std::memory_order_acquire));
}

bool LatestVideoFrameMailbox::markDeliveryScheduled()
{
    bool expected = false;
    return m_deliveryScheduled.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
}

void LatestVideoFrameMailbox::deliveryCompleted()
{
    m_deliveryScheduled.store(false, std::memory_order_release);
}

void LatestVideoFrameMailbox::clear()
{
    m_latestFrame.store({}, std::memory_order_release);
    m_deliveryScheduled.store(false, std::memory_order_release);
}

uint64_t LatestVideoFrameMailbox::coalescedFrames() const noexcept
{
    return m_coalescedFrames.load(std::memory_order_relaxed);
}
