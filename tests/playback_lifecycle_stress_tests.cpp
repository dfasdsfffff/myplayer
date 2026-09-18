#define SDL_MAIN_HANDLED

#include "packet_queue.h"
#include "playback_runtime.h"

#include <atomic>
#include <iostream>
#include <thread>

namespace {

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

} // namespace

int main()
{
    for (int iteration = 0; iteration < 1000; ++iteration) {
        PacketQueue queue;
        if (!Expect(queue.init() == 0, "packet queue should initialize"))
            return 1;
        queue.start();

        std::atomic_bool stopWorkers{false};
        std::atomic_bool snapshotIsValid{true};
        std::thread producer([&] {
            while (!stopWorkers.load(std::memory_order_acquire)) {
                AVPacket packet{};
                if (queue.put(&packet) < 0)
                    break;
            }
        });
        std::thread consumer([&] {
            while (!stopWorkers.load(std::memory_order_acquire)) {
                AVPacket packet{};
                const int result = queue.get(&packet, 0, nullptr);
                av_packet_unref(&packet);
                if (result < 0)
                    break;
            }
        });
        std::thread snapshotter([&] {
            for (int sample = 0; sample < 32; ++sample) {
                const auto snapshot = queue.snapshot();
                if (snapshot.packets < 0 || snapshot.bytes < 0 || snapshot.duration < 0) {
                    snapshotIsValid.store(false, std::memory_order_release);
                    break;
                }
                queue.flush();
            }
        });

        snapshotter.join();
        stopWorkers.store(true, std::memory_order_release);
        queue.abort();
        producer.join();
        consumer.join();
        const auto aborted = queue.snapshot();
        if (!Expect(snapshotIsValid.load(std::memory_order_acquire), "queue snapshots must remain coherent during concurrent operations") ||
            !Expect(aborted.aborted, "abort should be visible through the queue snapshot"))
            return 1;
    }

    for (int iteration = 0; iteration < 25; ++iteration) {
        auto runtime = PlaybackRuntime::Create();
        if (!Expect(runtime != nullptr, "playback runtime should create repeatedly"))
            return 1;
        if (!Expect(!runtime->controller().play(MediaSource{}), "an invalid open must fail without leaving a running session"))
            return 1;
        runtime->controller().stopAndWait();
    }

    return 0;
}
