#include <play_core/player.h>
#include <play_core/network_runtime.h>
#include <condition_variable>
#include <mutex>
int main() {
    play_core::Player player;
    std::mutex mutex; std::condition_variable cv; bool closed = false;
    auto subscription = player.subscribe([&](const play_core::PlayerEvent& e) {
        if (e.type == play_core::PlayerEvent::Type::Closed) { std::lock_guard lock(mutex); closed = true; cv.notify_one(); }
    });
    if (!player.close()) return 1;
    std::unique_lock lock(mutex);
    return cv.wait_for(lock,std::chrono::seconds(5),[&] { return closed; }) ? 0 : 1;
}
