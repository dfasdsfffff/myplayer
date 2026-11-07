#include <player_sdk_c/player.h>
#include "media_fixture_builder.h"
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
using namespace std::chrono_literals;
namespace {
void check(bool b, const char* text) { if (!b) throw std::runtime_error(text); }
struct Observer {
    std::mutex mutex;
    std::condition_variable cv;
    std::map<uint64_t, ps_event_info_v1> results;
    ps_player_handle player{};
    ps_event* frame{};
    ps_event* media{};
    const std::thread::id hostThread = std::this_thread::get_id();
    bool failed{}, destroyRejected{true}, closed{};
    unsigned duplicates{};
    ~Observer() { ps_event_release(frame); ps_event_release(media); }
    static void PS_CALL callback(void* user, const ps_event* e) {
        auto& o = *static_cast<Observer*>(user);
        std::lock_guard lock(o.mutex);
        o.failed |= std::this_thread::get_id() == o.hostThread;
        ps_event_info_v1 info{};
        if (ps_event_get_info_v1(e, &info) != PS_OK) o.failed = true;
        if (info.type == PS_EVENT_OPERATION) {
            if (o.results.contains(info.operation)) ++o.duplicates;
            o.results[info.operation] = info;
            o.destroyRejected &= ps_player_destroy(o.player) == PS_ERROR_INVALID_STATE;
        }
        if (info.type == PS_EVENT_VIDEO && !o.frame)
            o.failed |= ps_event_retain(e, &o.frame) != PS_OK;
        if (info.type == PS_EVENT_MEDIA_INFO && !o.media)
            o.failed |= ps_event_retain(e, &o.media) != PS_OK;
        o.closed |= info.type == PS_EVENT_CLOSED;
        o.cv.notify_all();
    }
    ps_event_info_v1 wait(int32_t code, const ps_request_v1& r, bool success = true) {
        check(code == PS_OK && r.operation, "Request rejected");
        std::unique_lock lock(mutex);
        check(cv.wait_for(lock, 10s, [&] { return results.contains(r.operation); }), "Operation timed out");
        auto info = results.at(r.operation);
        check(info.operation_session == r.session, "Session mismatch");
        if (success) check(info.operation_status == PS_OPERATION_SUCCEEDED, info.operation_error.message);
        return info;
    }
};
struct PlayerGuard {
    ps_player_handle p{};
    ~PlayerGuard() { ps_player_destroy(p); }
};
}
int main() {
    const auto dir = std::filesystem::temp_directory_path() /
        ("player-c-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directory(dir);
        const auto fixtures = BuildMediaFixtures(dir, 120);
        const auto path = dir / std::filesystem::path(u8"中文视频.y4m");
        std::filesystem::rename(fixtures.video, path);
        const auto utf8 = path.u8string();
        std::string location(utf8.begin(), utf8.end());
        Observer o; // must outlive guard
        PlayerGuard guard;
        ps_player_options_v1 options{}; ps_player_options_init_v1(&options);
        options.hardware_decode = PS_HARDWARE_DISABLED;
        ps_error_v1 error{};
        check(ps_player_create_v1(&options, Observer::callback, &o, &guard.p, &error) == PS_OK, error.message);
        o.player = guard.p;
        ps_source_v1 source{}; ps_source_init_v1(&source);
        source.location = location.c_str(); source.enable_audio = 0;
        ps_request_v1 r{};
        auto code = ps_player_open_v1(guard.p, &source, &r);
        location.assign(location.size(), '?');
        source.location = nullptr;
        o.wait(code, r);
        code = ps_player_play_v1(guard.p, &r); o.wait(code, r);
        {
            std::unique_lock lock(o.mutex);
            check(o.cv.wait_for(lock, 5s, [&] { return o.frame && o.media; }), "No video/media event");
        }
        code = ps_player_set_paused_v1(guard.p, 1, &r); o.wait(code, r);
        code = ps_player_seek_v1(guard.p, 1234, &r);
        const auto seek = o.wait(code, r);
        check(seek.has_requested_position && seek.requested_position_ms == 1234 &&
            seek.has_actual_position && seek.actual_position_ms == 1234, "Seek timestamps lost");
        code = ps_player_set_volume_v1(guard.p, .5, &r); o.wait(code, r);
        code = ps_player_set_playback_rate_v1(guard.p, 1, &r); o.wait(code, r);
        code = ps_player_select_subtitle_track_v1(guard.p, 0, 0, &r); o.wait(code, r);
        code = ps_player_stop_v1(guard.p, &r); o.wait(code, r);
        code = ps_player_close_v1(guard.p, &r); o.wait(code, r);
        {
            std::unique_lock lock(o.mutex);
            check(o.cv.wait_for(lock, 5s, [&] { return o.closed; }), "Missing closed event");
        }
        check(ps_player_destroy(guard.p) == PS_OK, "Destroy failed"); guard.p = nullptr;
        check(!o.failed && o.destroyRejected && !o.duplicates, "Callback contract failed");
        ps_video_frame_v1 video{};
        check(ps_event_get_video_v1(o.frame, &video) == PS_OK && video.width > 0 && video.height > 0, "Retained frame lost");
        check((video.bgra && video.bgra_size) || (video.planes[0].data && video.planes[0].size), "No pixel storage");
        volatile uint8_t pixel = video.bgra_size ? video.bgra[0] : video.planes[0].data[0]; (void)pixel;
        ps_media_info_v1 media{}; ps_track_info_v1 track{};
        check(ps_event_get_media_info_v1(o.media, &media) == PS_OK && media.width == video.width, "Media info lost");
        check(ps_event_get_track_v1(o.media, media.track_count, &track) == PS_ERROR_INVALID_ARGUMENT, "Invalid track accepted");
        ps_subtitle_v1 sub{};
        check(ps_event_get_subtitle_v1(o.frame, &sub) == PS_ERROR_INVALID_STATE, "Wrong event payload accepted");
        ps_event* second{};
        check(ps_event_retain(o.frame, &second) == PS_OK, "Retain retained event failed");
        ps_event_release(o.frame); o.frame = nullptr;
        check(ps_event_get_video_v1(second, &video) == PS_OK, "Second retain lost storage");
        ps_event_release(second);
        Observer audio;
        PlayerGuard audioGuard;
        check(ps_player_create_v1(&options, Observer::callback, &audio, &audioGuard.p, &error) == PS_OK, error.message);
        audio.player = audioGuard.p;
        const auto wavUtf8 = fixtures.wav.u8string();
        const std::string wavLocation(wavUtf8.begin(), wavUtf8.end());
        source.location = wavLocation.c_str(); source.enable_audio = 1;
        code = ps_player_open_v1(audioGuard.p, &source, &r); audio.wait(code, r);
        {
            std::unique_lock lock(audio.mutex);
            check(audio.cv.wait_for(lock, 5s, [&] { return audio.media != nullptr; }), "No audio media info");
            check(ps_event_get_media_info_v1(audio.media, &media) == PS_OK && media.track_count == 1, "Audio track count lost");
            check(ps_event_get_track_v1(audio.media, 0, &track) == PS_OK && track.type == PS_MEDIA_AUDIO && track.codec[0], "Audio track lost");
        }
        code = ps_player_close_v1(audioGuard.p, &r); audio.wait(code, r);
        check(ps_player_destroy(audioGuard.p) == PS_OK, "Audio destroy failed"); audioGuard.p = nullptr;
        std::filesystem::remove_all(dir);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        std::filesystem::remove_all(dir); return 1;
    }
}
