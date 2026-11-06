#include <play_core/player.h>
#include "media_fixture_builder.h"
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#include <stdexcept>
using namespace std::chrono_literals;
namespace {
void require(bool b, const char* message) { if (!b) throw std::runtime_error(message); }
struct Observer {
    std::mutex mutex; std::condition_variable cv;
    std::map<play_core::OperationId,play_core::OperationResult> results;
    std::shared_ptr<const play_core::VideoFrame> frame;
    std::int64_t position{0};
    unsigned duplicates{0};
    void event(const play_core::PlayerEvent& e) {
        std::lock_guard lock(mutex);
        if (e.type == play_core::PlayerEvent::Type::Operation) {
            if (results.contains(e.operation.operation)) ++duplicates;
            results[e.operation.operation] = e.operation;
        }
        if (e.video) frame = e.video;
        if (e.type == play_core::PlayerEvent::Type::Position) position = e.positionMs;
        cv.notify_all();
    }
    play_core::OperationResult wait(play_core::Request r, bool success = true) {
        require(bool(r),"Request rejected"); std::unique_lock lock(mutex);
        require(cv.wait_for(lock,10s,[&] { return results.contains(r.operation); }),"Operation timed out");
        auto result = results.at(r.operation);
        if (success && result.status != play_core::OperationStatus::Succeeded)
            throw std::runtime_error("Operation failed: " + result.error.message);
        return result;
    }
};
}
int main() {
    auto dir = std::filesystem::temp_directory_path() / ("player-sdk-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directory(dir);
        const auto fixture = BuildMediaFixtures(dir,120);
        const auto chinese = dir / std::filesystem::path(u8"中文视频.y4m");
        std::filesystem::rename(fixture.video,chinese);
        const auto utf8 = chinese.u8string();
        const play_core::MediaSource source{std::string(utf8.begin(),utf8.end())};
        Observer o;
        std::shared_ptr<const play_core::VideoFrame> retained;
        {
            play_core::Player player({play_core::HardwareDecode::Disabled});
            auto sub = player.subscribe([&](const auto& e) { o.event(e); });
            require(!player.open({""}),"Empty source accepted");
            require(!player.setVolume(2),"Invalid volume accepted");
            require(!player.setPlaybackRate(0),"Invalid speed accepted");
            o.wait(player.open(source));
            std::this_thread::sleep_for(150ms);
            { std::lock_guard lock(o.mutex); require(o.position <= 0,"Open advanced without play"); }
            o.wait(player.play());
            { std::unique_lock lock(o.mutex); require(o.cv.wait_for(lock,5s,[&] { return o.frame && o.position > 0; }),"No video/position"); }
            o.wait(player.setPaused(true));
            auto seek = o.wait(player.seek(1234));
            require(seek.requestedPositionMs == 1234 && seek.actualPositionMs == 1234,"Seek lost milliseconds");
            o.wait(player.stop()); o.wait(player.play());
            auto a = player.open(source); auto b = player.open(source); auto c = player.open(source);
            o.wait(a,false); o.wait(b,false); o.wait(c);
            o.wait(player.play());
            play_core::Player second({play_core::HardwareDecode::Disabled}); Observer other;
            auto otherSub = second.subscribe([&](const auto& e) { other.event(e); });
            other.wait(second.open(source)); other.wait(second.play());
            o.wait(player.close());
            { std::unique_lock lock(other.mutex); require(other.cv.wait_for(lock,5s,[&] { return other.frame != nullptr; }),"Closing one instance broke another"); }
            other.wait(second.close());
            { std::lock_guard lock(o.mutex); retained = o.frame; require(o.duplicates == 0,"Duplicate terminal results"); }
            o.wait(player.close());
            o.wait(player.open(source)); o.wait(player.close());
            sub.reset();
        }
        {
            play_core::Player player({play_core::HardwareDecode::Disabled});
            Observer observer;
            std::mutex gateMutex; std::condition_variable gateCv;
            bool entered = false, released = false;
            auto slow = player.subscribe([&](const auto& event) {
                observer.event(event);
                if (event.type == play_core::PlayerEvent::Type::State && event.state == play_core::PlayerState::Opening) {
                    std::unique_lock lock(gateMutex); entered = true; gateCv.notify_all();
                    gateCv.wait_for(lock,2s,[&] { return released; });
                }
            });
            const auto opening = player.open(source);
            { std::unique_lock lock(gateMutex); require(gateCv.wait_for(lock,5s,[&] { return entered; }),"Slow callback not entered"); }
            const auto closing = player.close();
            std::vector<play_core::Request> accepted;
            for (int i=0;i<256;++i) { auto r = player.setVolume(.5); if (r) accepted.push_back(r); }
            require(accepted.size() <= 126,"Slow callback allowed unbounded operations");
            { std::lock_guard lock(gateMutex); released = true; } gateCv.notify_all();
            observer.wait(opening,false); observer.wait(closing);
            for (auto r : accepted) observer.wait(r);
            slow.reset();
        }
        {
            play_core::Player player;
            Observer observer;
            auto throwing = player.subscribe([](const auto&) { throw std::runtime_error("host callback"); });
            auto subscription = player.subscribe([&](const auto& event) {
                observer.event(event);
                if (event.type == play_core::PlayerEvent::Type::MediaInfo) player.close();
            });
            const auto opening = player.open(source); observer.wait(opening,false);
            observer.wait(player.close());
            subscription.reset(); throwing.reset();
        }
        require(retained && retained->width == 64,"Retained frame invalid after destruction");
        require(!retained->bgra.empty() || retained->planes[0].data,"Retained storage missing");
        std::filesystem::remove_all(dir);
        std::cout << "SDK lifecycle, precision, session replacement, multi-instance and retained frames passed\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr << e.what() << '\n'; std::filesystem::remove_all(dir); return 1;
    }
}
