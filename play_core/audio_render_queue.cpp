#include "audio_render_queue.h"

#include "audio_output.h"
#include "video_state.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace {
constexpr size_t kBufferedAudioMilliseconds = 500;
}

AudioRenderQueue::~AudioRenderQueue()
{
    stop();
}

bool AudioRenderQueue::start(VideoState* state, const AudioParams& target)
{
    stop();
    if (target.bytes_per_sec <= 0 || target.frame_size <= 0)
        return false;

    const size_t requestedCapacity = static_cast<size_t>(target.bytes_per_sec) * kBufferedAudioMilliseconds / 1000;
    m_capacity = std::max<size_t>(static_cast<size_t>(target.frame_size),
        requestedCapacity - requestedCapacity % static_cast<size_t>(target.frame_size));
    try {
        m_buffer.assign(m_capacity, 0);
    } catch (...) {
        m_capacity = 0;
        return false;
    }
    m_readPosition.store(0, std::memory_order_release);
    m_writePosition.store(0, std::memory_order_release);
    m_underruns.store(0, std::memory_order_release);
    m_producerClock.store(NAN, std::memory_order_release);
    m_producerClockSerial.store(0, std::memory_order_release);
    m_stopping.store(false, std::memory_order_release);
    if (state)
        m_producer = std::thread(&AudioRenderQueue::produce, this, state);
    return true;
}

void AudioRenderQueue::stop()
{
    m_stopping.store(true, std::memory_order_release);
    m_spaceAvailable.notify_all();
    if (m_producer.joinable())
        m_producer.join();
}

size_t AudioRenderQueue::availableToRead() const noexcept
{
    const size_t write = m_writePosition.load(std::memory_order_acquire);
    const size_t read = m_readPosition.load(std::memory_order_acquire);
    return write - read;
}

size_t AudioRenderQueue::availableToWrite() const noexcept
{
    return m_capacity - availableToRead();
}

size_t AudioRenderQueue::write(const uint8_t* source, size_t bytes)
{
    if (!source || !bytes || !m_capacity)
        return 0;

    size_t written = 0;
    std::unique_lock<std::mutex> lock(m_writeMutex);
    while (written < bytes && !m_stopping.load(std::memory_order_acquire)) {
        m_spaceAvailable.wait(lock, [this] {
            return m_stopping.load(std::memory_order_acquire) || availableToWrite() > 0;
        });
        if (m_stopping.load(std::memory_order_acquire))
            break;

        const size_t write = m_writePosition.load(std::memory_order_relaxed);
        const size_t contiguous = std::min({bytes - written, availableToWrite(), m_capacity - write % m_capacity});
        std::memcpy(m_buffer.data() + write % m_capacity, source + written, contiguous);
        m_writePosition.store(write + contiguous, std::memory_order_release);
        written += contiguous;
    }
    return written;
}

size_t AudioRenderQueue::read(uint8_t* destination, size_t bytes) noexcept
{
    if (!destination || !bytes)
        return 0;

    const size_t readable = std::min(bytes, availableToRead());
    if (readable) {
        const size_t read = m_readPosition.load(std::memory_order_relaxed);
        const size_t first = std::min(readable, m_capacity - read % m_capacity);
        std::memcpy(destination, m_buffer.data() + read % m_capacity, first);
        if (readable > first)
            std::memcpy(destination + first, m_buffer.data(), readable - first);
        m_readPosition.store(read + readable, std::memory_order_release);
        m_spaceAvailable.notify_one();
    }
    if (readable < bytes) {
        std::memset(destination + readable, 0, bytes - readable);
        m_underruns.fetch_add(1, std::memory_order_relaxed);
    }
    return readable;
}

void AudioRenderQueue::flush()
{
    std::lock_guard<std::mutex> lock(m_writeMutex);
    m_readPosition.store(m_writePosition.load(std::memory_order_acquire), std::memory_order_release);
    m_producerClock.store(NAN, std::memory_order_release);
    m_spaceAvailable.notify_all();
}

uint64_t AudioRenderQueue::underruns() const noexcept
{
    return m_underruns.load(std::memory_order_relaxed);
}

size_t AudioRenderQueue::capacityBytes() const noexcept
{
    return m_capacity;
}

size_t AudioRenderQueue::pendingBytes() const noexcept
{
    return availableToRead();
}

double AudioRenderQueue::producerClock() const noexcept
{
    return m_producerClock.load(std::memory_order_acquire);
}

int AudioRenderQueue::producerClockSerial() const noexcept
{
    return m_producerClockSerial.load(std::memory_order_acquire);
}

void AudioRenderQueue::setProducerClock(double clock, int serial) noexcept
{
    m_producerClockSerial.store(serial, std::memory_order_release);
    m_producerClock.store(clock, std::memory_order_release);
}

void AudioRenderQueue::produce(VideoState* state)
{
    while (!m_stopping.load(std::memory_order_acquire) &&
           !state->session.abort_request.load(std::memory_order_acquire)) {
        const int preparedBytes = AudioOutput::DecodeFrame(state);
        if (preparedBytes > 0 && state->audio.audio_buf) {
            write(state->audio.audio_buf, static_cast<size_t>(preparedBytes));
            setProducerClock(state->audio.audio_clock, state->audio.audio_clock_serial);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
}
