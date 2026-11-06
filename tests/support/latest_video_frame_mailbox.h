#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "video_frame.h"

class LatestVideoFrameMailbox final
{
public:
    void publish(std::shared_ptr<VideoFrame> frame);
    std::shared_ptr<VideoFrame> takeLatest();
    bool hasPending() const;
    bool markDeliveryScheduled();
    void deliveryCompleted();
    void clear();
    uint64_t coalescedFrames() const noexcept;

private:
    std::atomic<std::shared_ptr<VideoFrame>> m_latestFrame;
    std::atomic<bool> m_deliveryScheduled{false};
    std::atomic<uint64_t> m_coalescedFrames{0};
};
