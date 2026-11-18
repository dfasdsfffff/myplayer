#include <play_core/player.h>
#include "../../core/src/player_event_queue.h"
#include "media_fixture_builder.h"
#include <condition_variable>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#include <stdexcept>
using namespace std::chrono_literals;
namespace {
void require(bool b, const char* message) { if (!b) throw std::runtime_error(message); }
void testEventQueueSessionIsolation() {
    using namespace play_core;
    using Type = PlayerEvent::Type;
    std::deque<PlayerEvent> queue;
    PlayerEvent oldSubtitle; oldSubtitle.type = Type::Subtitle; oldSubtitle.session = 1;
    PlayerEvent newSubtitle = oldSubtitle; newSubtitle.session = 2;
    auto subtitle = std::make_shared<SubtitleFrame>(); subtitle->assOrText = "new session";
    newSubtitle.subtitle = subtitle;
    PlayerEvent oldResult; oldResult.type = Type::Operation; oldResult.session = 1;
    oldResult.operation = {42, 1, OperationStatus::Superseded};
    require(detail::enqueuePlayerEvent(queue, oldSubtitle, 1), "Old session subtitle not queued");
    require(detail::enqueuePlayerEvent(queue, oldResult, 1), "Old session result not queued");
    detail::discardInactiveEvents(queue, 2);
    require(detail::enqueuePlayerEvent(queue, newSubtitle, 2), "New session subtitle not queued");
    require(!detail::enqueuePlayerEvent(queue, oldSubtitle, 2), "Late subtitle was accepted");
    oldResult.operation.operation = 43;
    require(detail::enqueuePlayerEvent(queue, oldResult, 2), "Late terminal result was dropped");
    require(queue.size() == 3 && queue[0].operation.operation == 42 && queue[1].subtitle == subtitle &&
        queue[2].operation.operation == 43, "Late subtitle erased active data or terminal results");
    newSubtitle.subtitle.reset();
    require(detail::enqueuePlayerEvent(queue, newSubtitle, 2) && queue.size() == 3 && !queue[1].subtitle,
        "Active-session subtitle clear was not coalesced");
}
struct Observer {
    std::mutex mutex; std::condition_variable cv;
    std::map<play_core::OperationId,play_core::OperationResult> results;
    std::shared_ptr<const play_core::VideoFrame> frame;
    std::int64_t position{0};
    unsigned duplicates{0};
    unsigned frames{0};
    play_core::PlayerState state{play_core::PlayerState::Closed};
    void event(const play_core::PlayerEvent& e) {
        std::lock_guard lock(mutex);
        if (e.type == play_core::PlayerEvent::Type::Operation) {
            if (results.contains(e.operation.operation)) ++duplicates;
            results[e.operation.operation] = e.operation;
        }
        if (e.video) { frame = e.video; ++frames; }
        if (e.type == play_core::PlayerEvent::Type::State) state = e.state;
        if (e.type == play_core::PlayerEvent::Type::Position) position = e.positionMs;
        cv.notify_all();
    }
    play_core::OperationResult wait(play_core::Request r, bool success = true) {
        require(bool(r),"Request rejected"); std::unique_lock lock(mutex);
        require(cv.wait_for(lock,10s,[&] { return results.contains(r.operation); }),"Operation timed out");
        auto result = results.at(r.operation);
        if (success && result.status != play_core::OperationStatus::Succeeded)
            throw std::runtime_error("Operation " + std::to_string(r.operation) + " failed: " + result.error.message +
                " (" + std::to_string(result.error.nativeCode) + ")");
        return result;
    }
};
void testFrameSteps(const std::filesystem::path& path, bool variable) {
    Observer o;
    play_core::Player player({play_core::HardwareDecode::Disabled});
    auto sub = player.subscribe([&](const auto& e) { o.event(e); });
    require(!player.stepFrame(0) && !player.stepFrame(2), "Invalid frame direction accepted");
    require(o.wait(player.stepFrame(1), false).status == play_core::OperationStatus::Failed,
        "Closed player accepted stepping");
    const auto u8 = path.u8string();
    o.wait(player.open({std::string(u8.begin(), u8.end())}));
    const auto timestamp = [variable](int index) -> int64_t {
        return variable ? (index / 2) * 160 + (index % 2) * 40 : index * 100;
    };
    auto step = [&](int direction, int index) {
        const auto result = [&] {
            try { return o.wait(player.stepFrame(direction)); }
            catch (const std::exception& e) {
                throw std::runtime_error(std::string(variable ? "VFR " : "CFR ") + "step " +
                    std::to_string(direction) + " to frame " + std::to_string(index) + ": " + e.what());
            }
        }();
        if (result.actualPositionMs != timestamp(index))
            throw std::runtime_error("Non-adjacent frame: expected " + std::to_string(timestamp(index)) +
                ", got " + std::to_string(result.actualPositionMs.value_or(-1)));
        require(!result.requestedPositionMs, "Frame step reported an invented seek target");
        std::lock_guard lock(o.mutex);
        require(o.frame && o.frame->timestampMs == result.actualPositionMs, "Step completed before target publication");
        require(o.state == play_core::PlayerState::Paused, "Step did not leave player paused");
        if (variable) {
            require(o.frame->planes[0].data != nullptr, "Step fixture lost pixels");
            const int luma = o.frame->planes[0].data.get()[0];
            require(std::abs(luma - (32 + index * 6)) <= 3, "Frame content does not match its timestamp");
        }
    };
    step(1, 0);
    step(-1, 0);
    o.wait(player.setPlaybackRate(0.1));
    for (int i = 1; i < 24; ++i) {
        if (i == 12) o.wait(player.setPlaybackRate(2.0));
        step(1, i);
    }
    if (variable) { step(1, 23); step(1, 23); }
    for (int i = 22; i >= 0; --i) step(-1, i);
    step(-1, 0);
    unsigned before;
    std::this_thread::sleep_for(180ms);
    { std::lock_guard lock(o.mutex);
      require(o.frame && o.frame->timestampMs == 0 && o.position == 0, "Paused stepping advanced the frame or clock"); }
    o.wait(player.setPlaybackRate(1.0));
    o.wait(player.play());
    { std::unique_lock lock(o.mutex);
      require(o.cv.wait_for(lock, 3s, [&] { return o.frame && o.frame->timestampMs >= timestamp(3); }),
          "Resume after stepping remained stuck paused"); }
    o.wait(player.setPaused(true));
    { std::lock_guard lock(o.mutex); before = o.frames; }
    o.wait(player.seek(1500));
    int seekIndex = -1;
    { std::unique_lock lock(o.mutex);
      require(o.cv.wait_for(lock, 3s, [&] { return o.frames > before && o.frame && o.frame->timestampMs >= timestamp(5); }),
          "Paused seek did not publish a later frame");
      for (int i = 0; i < (variable ? 24 : 120); ++i)
          if (o.frame->timestampMs == timestamp(i)) seekIndex = i;
    }
    require(seekIndex > 0, "Seek fixture did not land on a known later frame");
    step(-1, seekIndex - 1);
    const auto forward = o.wait(player.stepFrame(1));
    const auto backward = o.wait(player.stepFrame(-1));
    require(forward.actualPositionMs && backward.actualPositionMs && *backward.actualPositionMs < *forward.actualPositionMs,
        "Stepping after ordinary playback failed");
    for (int i = 0; i < 8; ++i) {
        const auto interrupted = player.stepFrame(1);
        const auto pause = player.setPaused(true);
        const auto replacement = player.stepFrame(-1);
        o.wait(interrupted, false);
        o.wait(pause);
        const auto replaced = o.wait(replacement);
        { std::lock_guard lock(o.mutex);
          require(o.frame && o.frame->timestampMs == replaced.actualPositionMs,
              "Cancelled scan published into its replacement operation"); }
    }
    std::vector<play_core::Request> burst;
    for (int i = 0; i < 8; ++i) burst.push_back(player.stepFrame(1));
    o.wait(player.close());
    for (auto r : burst) o.wait(r, false);
    o.wait(player.open({std::string(u8.begin(), u8.end())}));
    step(1, 0);
    o.wait(player.close());
    { std::lock_guard lock(o.mutex); require(!o.duplicates, "Frame step completed more than once"); }
}
}
int main() {
    auto dir = std::filesystem::temp_directory_path() / ("player-sdk-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        testEventQueueSessionIsolation();
        std::filesystem::create_directory(dir);
        const auto fixture = BuildMediaFixtures(dir,120);
        testFrameSteps(fixture.video, false);
        testFrameSteps(BuildFrameStepFixture(dir), true);
        const auto chinese = dir / std::filesystem::path(u8"中文视频.y4m");
        std::filesystem::rename(fixture.video,chinese);
        const auto utf8 = chinese.u8string();
        const play_core::MediaSource source{std::string(utf8.begin(),utf8.end())};
        Observer o;
        std::shared_ptr<const play_core::VideoFrame> retained;
        std::uint8_t retainedPixel = 0;
        {
            play_core::Player player({play_core::HardwareDecode::Disabled});
            auto sub = player.subscribe([&](const auto& e) { o.event(e); });
            require(!player.open({""}),"Empty source accepted");
            require(!player.setVolume(2),"Invalid volume accepted");
            require(!player.setPlaybackRate(0),"Invalid speed accepted");
            const auto first = player.open(source);
            o.wait(first);
            const auto ready = player.snapshot();
            require(ready.session == first.session && ready.state == play_core::PlayerState::Ready &&
                ready.mediaInfo && ready.mediaInfo->width == 64, "Ready snapshot missing current media");
            std::this_thread::sleep_for(150ms);
            { std::lock_guard lock(o.mutex); require(o.position <= 0,"Open advanced without play"); }
            o.wait(player.play());
            { std::unique_lock lock(o.mutex); require(o.cv.wait_for(lock,5s,[&] { return o.frame && o.position > 0; }),"No video/position"); }
            const auto playing = player.snapshot();
            require(playing.session == first.session && playing.state == play_core::PlayerState::Playing &&
                playing.positionMs.value_or(0) > 0 && playing.diagnostics.videoDecodeBackend == play_core::VideoDecodeBackend::Software,
                "Playing snapshot missing position or observed software decoder");
            { std::lock_guard lock(o.mutex);
              retained = o.frame;
              require(retained->format == play_core::VideoFrameFormat::Yuv420P && retained->planes[0].data,
                  "Expected shared YUV frame");
              retainedPixel = retained->planes[0].data.get()[0];
            }
            o.wait(player.setPaused(true));
            auto seek = o.wait(player.seek(1234));
            require(seek.requestedPositionMs == 1234 && seek.actualPositionMs == 1234,"Seek lost milliseconds");
            o.wait(player.stop()); o.wait(player.play());
            auto a = player.open(source); auto b = player.open(source);
            const auto audioPath = fixture.wav.u8string();
            const auto audio = player.open({std::string(audioPath.begin(), audioPath.end())});
            const auto switching = player.snapshot();
            require(switching.session == audio.session && (!switching.mediaInfo || switching.mediaInfo->width == 0) &&
                switching.diagnostics.videoDecodeBackend == play_core::VideoDecodeBackend::Unknown,
                "Audio session inherited previous video state");
            o.wait(a,false); o.wait(b,false); o.wait(audio);
            const auto audioReady = player.snapshot();
            require(audioReady.session == audio.session && audioReady.state == play_core::PlayerState::Ready &&
                audioReady.mediaInfo && audioReady.mediaInfo->width == 0, "New session did not replace media snapshot");
            const auto current = player.open(source);
            o.wait(current);
            o.wait(player.play());
            play_core::Player second({play_core::HardwareDecode::Disabled}); Observer other;
            auto otherSub = second.subscribe([&](const auto& e) { other.event(e); });
            other.wait(second.open(source)); other.wait(second.play());
            o.wait(player.close());
            const auto closed = player.snapshot();
            require(closed.session == current.session && closed.state == play_core::PlayerState::Closed &&
                !closed.mediaInfo && !closed.positionMs && closed.diagnostics.videoDecodeBackend == play_core::VideoDecodeBackend::Unknown,
                "Closed snapshot retained media or decoder diagnostics");
            require(ready.mediaInfo->width == 64, "Retained media snapshot no longer valid");
            { std::unique_lock lock(other.mutex); require(other.cv.wait_for(lock,5s,[&] { return other.frame != nullptr; }),"Closing one instance broke another"); }
            other.wait(second.close());
            { std::lock_guard lock(o.mutex); require(o.duplicates == 0,"Duplicate terminal results"); }
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
        require((retained->bgra && !retained->bgra->empty()) || retained->planes[0].data,"Retained storage missing");
        require(retained->planes[0].data.get()[0] == retainedPixel,"Retained pixel changed after decoder reuse and close");
        std::filesystem::remove_all(dir);
        std::cout << "SDK lifecycle, precision, session replacement, multi-instance and retained frames passed\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr << e.what() << '\n'; std::filesystem::remove_all(dir); return 1;
    }
}
