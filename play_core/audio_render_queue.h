#pragma once

#include "av_types.h"

#include <atomic>
#include <condition_variable>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

struct VideoState;

class AudioRenderQueue final {
public:
    AudioRenderQueue() = default;
    ~AudioRenderQueue();

    AudioRenderQueue(const AudioRenderQueue&) = delete;
    AudioRenderQueue& operator=(const AudioRenderQueue&) = delete;

    bool start(VideoState* state, const AudioParams& target);
    void stop();
    size_t read(uint8_t* destination, size_t bytes) noexcept;
    void flush();
    [[nodiscard]] uint64_t underruns() const noexcept;

    size_t write(const uint8_t* source, size_t bytes);
    [[nodiscard]] size_t capacityBytes() const noexcept;
    [[nodiscard]] size_t pendingBytes() const noexcept;
    [[nodiscard]] double producerClock() const noexcept;
    [[nodiscard]] int producerClockSerial() const noexcept;
    void setProducerClock(double clock, int serial) noexcept;

private:
    void produce(VideoState* state);
    [[nodiscard]] size_t availableToRead() const noexcept;
    [[nodiscard]] size_t availableToWrite() const noexcept;

    std::vector<uint8_t> m_buffer;
    size_t m_capacity{0};
    std::atomic_size_t m_readPosition{0};
    std::atomic_size_t m_writePosition{0};
    std::atomic_bool m_stopping{false};
    std::atomic_uint64_t m_underruns{0};
    std::atomic<double> m_producerClock{NAN};
    std::atomic_int m_producerClockSerial{0};
    std::mutex m_writeMutex;
    std::condition_variable m_spaceAvailable;
    std::thread m_producer;
};
