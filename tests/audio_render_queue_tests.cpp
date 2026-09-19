#define SDL_MAIN_HANDLED

#include "audio_render_queue.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

namespace {
bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

AudioParams StereoS16Target()
{
    AudioParams target{};
    target.fmt = AV_SAMPLE_FMT_S16;
    target.freq = 1000;
    av_channel_layout_default(&target.ch_layout, 1);
    target.frame_size = sizeof(int16_t);
    target.bytes_per_sec = target.freq * target.frame_size;
    return target;
}
}

int main()
{
    AudioRenderQueue queue;
    AudioParams target = StereoS16Target();
    if (!Expect(queue.start(nullptr, target), "queue should start with a valid target"))
        return 1;

    const std::vector<uint8_t> source(queue.capacityBytes(), 0x5a);
    const size_t accepted = queue.write(source.data(), source.size());
    if (!Expect(accepted == queue.capacityBytes(), "queue must bound pending PCM to its configured capacity") ||
        !Expect(queue.pendingBytes() == queue.capacityBytes(), "full queue must report bounded pending PCM"))
        return 1;

    std::array<uint8_t, 32> firstRead{};
    if (!Expect(queue.read(firstRead.data(), firstRead.size()) == firstRead.size(), "read should return prepared PCM") ||
        !Expect(std::all_of(firstRead.begin(), firstRead.end(), [](uint8_t value) { return value == 0x5a; }),
            "read must preserve prepared PCM bytes"))
        return 1;

    queue.write(source.data(), firstRead.size());

    std::atomic_bool writerStarted{false};
    std::atomic_bool writerFinished{false};
    std::thread blockedWriter([&] {
        writerStarted.store(true, std::memory_order_release);
        const std::array<uint8_t, 8> morePcm{1, 2, 3, 4, 5, 6, 7, 8};
        queue.write(morePcm.data(), morePcm.size());
        writerFinished.store(true, std::memory_order_release);
    });
    while (!writerStarted.load(std::memory_order_acquire))
        std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (!Expect(!writerFinished.load(std::memory_order_acquire), "producer must block while the PCM ring is full"))
        return 1;

    std::vector<uint8_t> drain(queue.capacityBytes());
    queue.read(drain.data(), drain.size());
    blockedWriter.join();
    if (!Expect(writerFinished.load(std::memory_order_acquire), "consumer read must wake a blocked producer"))
        return 1;

    queue.flush();
    std::array<uint8_t, 16> underrun{};
    underrun.fill(0xff);
    if (!Expect(queue.read(underrun.data(), underrun.size()) == 0, "empty queue must report an underrun") ||
        !Expect(std::all_of(underrun.begin(), underrun.end(), [](uint8_t value) { return value == 0; }),
            "underrun must fill the callback buffer with silence") ||
        !Expect(queue.underruns() == 1, "underrun counter must increment once per empty read"))
        return 1;

    queue.write(source.data(), queue.capacityBytes());
    std::atomic_bool stopWriterStarted{false};
    std::atomic_bool stopWriterFinished{false};
    std::thread stopBlockedWriter([&] {
        stopWriterStarted.store(true, std::memory_order_release);
        const std::array<uint8_t, 8> morePcm{1, 2, 3, 4, 5, 6, 7, 8};
        queue.write(morePcm.data(), morePcm.size());
        stopWriterFinished.store(true, std::memory_order_release);
    });
    while (!stopWriterStarted.load(std::memory_order_acquire))
        std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    queue.stop();
    stopBlockedWriter.join();
    if (!Expect(stopWriterFinished.load(std::memory_order_acquire), "stop must wake a blocked producer"))
        return 1;

    av_channel_layout_uninit(&target.ch_layout);
    return 0;
}
