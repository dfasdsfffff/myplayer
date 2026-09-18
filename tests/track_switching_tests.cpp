#include "video_state.h"

#ifdef main
#undef main
#endif

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <thread>

namespace {
bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

int main()
{
    SessionState state;
    int publishedIndex = 1;
    std::atomic_bool routing{false};
    std::atomic_bool replacementFinished{false};

    std::thread route([&] {
        std::shared_lock<std::shared_mutex> lock(state.trackMutex);
        routing.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    });
    while (!routing.load(std::memory_order_acquire))
        std::this_thread::yield();

    std::thread replace([&] {
        std::unique_lock<std::shared_mutex> lock(state.trackMutex);
        publishedIndex = 2;
        replacementFinished.store(true, std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const bool waited = !replacementFinished.load(std::memory_order_acquire);
    route.join();
    replace.join();

    return Expect(waited && publishedIndex == 2 && replacementFinished.load(std::memory_order_acquire),
        "track replacement waits for routing and publishes atomically") ? 0 : 1;
}
