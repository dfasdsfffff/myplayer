#include "clock.h"

#include <atomic>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>

#ifdef main
#undef main
#endif

namespace {
bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;
    std::atomic<int> queueSerial{7};
    Clock clock;
    clock.init(&queueSerial);

    std::atomic_bool start{false};
    std::atomic_bool failed{false};
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&] {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            for (int sample = 0; sample < 10000; ++sample) {
                const ClockSnapshot snapshot = clock.snapshot();
                if (!std::isnan(snapshot.pts) &&
                    (std::fabs(snapshot.ptsDrift - (snapshot.pts - snapshot.lastUpdated)) > 1e-9 ||
                        snapshot.serial != 7 || snapshot.speed <= 0.0))
                    failed.store(true, std::memory_order_release);
                (void)clock.get();
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (int value = 0; value < 10000; ++value) {
        const double pts = static_cast<double>(value);
        clock.set_at(pts, 7, pts + 0.25);
        clock.setPaused((value % 2) == 0);
    }
    for (auto& reader : readers)
        reader.join();

    return Expect(!failed.load(std::memory_order_acquire), "readers never observe a torn clock snapshot") ? 0 : 1;
}
