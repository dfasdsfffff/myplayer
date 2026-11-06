#include <play_core/player.h>
#include <condition_variable>
#include <iostream>
#include <mutex>
int main(int argc,char** argv) {
    if (argc != 2) { std::cerr << "Usage: player_sdk_console <UTF-8 path or URL>\n"; return 2; }
    try {
        play_core::Player player;
        std::mutex mutex; std::condition_variable cv; bool done = false; bool failed = false;
        auto subscription = player.subscribe([&](const play_core::PlayerEvent& e) {
            if (e.type == play_core::PlayerEvent::Type::Video && e.video)
                std::cout << "Frame " << e.video->width << 'x' << e.video->height << '\n';
            if (e.type == play_core::PlayerEvent::Type::Error || e.type == play_core::PlayerEvent::Type::Ended) {
                std::lock_guard lock(mutex); failed = e.type == play_core::PlayerEvent::Type::Error;
                if (failed) std::cerr << e.error.message << '\n';
                done = true; cv.notify_one();
            }
        });
        auto request = player.open({argv[1]});
        if (!request) { std::cerr << request.error.message << '\n'; return 1; }
        player.play();
        { std::unique_lock lock(mutex); cv.wait(lock,[&] { return done; }); }
        player.close(); subscription.reset();
        // Player destruction joins close/workers. Never destroy inside a callback.
        return failed ? 1 : 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
