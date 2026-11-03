#define SDL_MAIN_HANDLED

#include "media_fixture_builder.h"
#include "playback_runtime.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {
using namespace std::chrono_literals;
constexpr auto transitionTimeout = 5s;
constexpr auto quietInterval = 250ms; // More than two fixture frame periods (10 fps).
constexpr int videoFrameCount = 120;

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

class FixtureDirectory {
public:
    FixtureDirectory()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto root = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto candidate = root / ("myplayer-playback-fixtures-" + std::to_string(stamp) +
                                          "-" + std::to_string(attempt));
            // Atomic directory creation also isolates concurrent invocations of this executable.
            if (std::filesystem::create_directory(candidate)) {
                path = candidate;
                return;
            }
        }
        throw std::runtime_error("cannot reserve isolated playback fixture directory");
    }

    ~FixtureDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        if (error)
            std::cerr << "Cannot remove playback fixtures: " << error.message() << '\n';
    }

    std::filesystem::path path;
};

enum class FrameTone { Unknown, Dark, Bright };

FrameTone ToneOf(const VideoFrame& frame)
{
    if (frame.width != 64 || frame.height != 48)
        return FrameTone::Unknown;
    if (frame.format == VideoFrameFormat::Yuv420P) {
        const auto& plane = frame.planes[0];
        if (!plane.data || plane.stride < frame.width || plane.height < frame.height)
            return FrameTone::Unknown;
        const int luma = plane.data.get()[(frame.height / 2) * plane.stride + frame.width / 2];
        return luma < 100 ? FrameTone::Dark : luma > 150 ? FrameTone::Bright : FrameTone::Unknown;
    }
    if (frame.bytesPerLine < frame.width * 4 ||
        frame.bgra.size() < static_cast<std::size_t>(frame.bytesPerLine * frame.height))
        return FrameTone::Unknown;
    // With the fixture's fixed chroma, green separates both tones even after BGRA conversion.
    const int green = frame.bgra[(frame.height / 2) * frame.bytesPerLine + (frame.width / 2) * 4 + 1];
    return green < 40 ? FrameTone::Dark : green > 100 ? FrameTone::Bright : FrameTone::Unknown;
}

struct Observations {
    std::size_t frames{0};
    std::size_t darkFrames{0};
    std::size_t brightFrames{0};
    std::size_t mediaInfos{0};
    std::size_t openings{0};
    std::size_t playings{0};
    std::size_t stops{0};
    std::size_t stopFinished{0};
    std::size_t pauseChanges{0};
    std::size_t event{0};
    std::size_t lastOpeningEvent{0};
    std::size_t lastPlayingEvent{0};
    std::size_t lastStopFinishedEvent{0};
    bool paused{false};
    bool failed{false};
    MediaInfo mediaInfo;
};

class PlaybackObserver {
public:
    void connect(PlaybackRuntime& runtime)
    {
        connections.emplace_back(runtime.SigVideoFrame.connect([this](const std::shared_ptr<VideoFrame>& frame) {
            if (!frame)
                return;
            const FrameTone tone = ToneOf(*frame);
            std::lock_guard<std::mutex> lock(mutex);
            ++observations.frames;
            observations.darkFrames += tone == FrameTone::Dark;
            observations.brightFrames += tone == FrameTone::Bright;
            changed.notify_all();
        }));
        connections.emplace_back(runtime.SigMediaInfo.connect([this](const MediaInfo& info) {
            std::lock_guard<std::mutex> lock(mutex);
            observations.mediaInfo = info;
            ++observations.mediaInfos;
            changed.notify_all();
        }));
        connections.emplace_back(runtime.SigPlaybackStatus.connect([this](const PlaybackStatus& status) {
            std::lock_guard<std::mutex> lock(mutex);
            const auto event = ++observations.event;
            switch (status.state) {
            case PlaybackState::Opening:
                ++observations.openings;
                observations.lastOpeningEvent = event;
                break;
            case PlaybackState::Playing:
                ++observations.playings;
                observations.lastPlayingEvent = event;
                break;
            case PlaybackState::Stopped:
                ++observations.stops;
                break;
            case PlaybackState::Failed:
                observations.failed = true;
                std::cerr << "Playback failed, error=" << static_cast<int>(status.error)
                          << ": " << status.message << '\n';
                break;
            case PlaybackState::Buffering:
            case PlaybackState::Reconnecting:
                break;
            }
            changed.notify_all();
        }));
        connections.emplace_back(runtime.SigPauseStat.connect([this](bool paused) {
            std::lock_guard<std::mutex> lock(mutex);
            observations.paused = paused;
            ++observations.pauseChanges;
            changed.notify_all();
        }));
        connections.emplace_back(runtime.SigStopFinished.connect([this] {
            std::lock_guard<std::mutex> lock(mutex);
            ++observations.stopFinished;
            observations.lastStopFinishedEvent = ++observations.event;
            changed.notify_all();
        }));
    }

    Observations snapshot()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return observations;
    }

    template<typename Predicate>
    bool wait(Predicate predicate, const char* message)
    {
        std::unique_lock<std::mutex> lock(mutex);
        const bool ready = changed.wait_for(lock, transitionTimeout, [&] {
            return observations.failed || predicate(observations);
        });
        return Expect(ready && !observations.failed && predicate(observations), message);
    }

    bool expectQuiet(const char* message)
    {
        std::unique_lock<std::mutex> lock(mutex);
        const auto frames = observations.frames;
        const bool activity = changed.wait_for(lock, quietInterval, [&] {
            return observations.failed || observations.frames != frames;
        });
        return Expect(!activity, message);
    }

private:
    std::mutex mutex;
    std::condition_variable changed;
    Observations observations;
    std::vector<sigslot::scoped_connection> connections;
};

bool StartVideo(PlaybackRuntime& runtime, PlaybackObserver& observer, const std::filesystem::path& video)
{
    const auto before = observer.snapshot();
    if (!Expect(runtime.controller().play(video.string()), "generated video opens") ||
        !observer.wait([&](const Observations& state) {
            return state.playings > before.playings && state.mediaInfos > before.mediaInfos &&
                   state.darkFrames > before.darkFrames;
        }, "each video session publishes Playing, metadata, and a new opening-tone frame"))
        return false;
    const auto state = observer.snapshot();
    return Expect(state.openings > before.openings && state.lastOpeningEvent < state.lastPlayingEvent,
                  "video enters Opening before Playing") &&
           Expect(state.mediaInfo.seekable && state.mediaInfo.duration &&
                      state.mediaInfo.duration->count() >= videoFrameCount * 100 - 100 &&
                      state.mediaInfo.width == 64 && state.mediaInfo.height == 48,
                  "video metadata exposes generated duration, seekability, and dimensions");
}

bool TogglePause(PlaybackRuntime& runtime, PlaybackObserver& observer, bool paused)
{
    const auto before = observer.snapshot();
    runtime.controller().pause();
    // Paused is not a PlaybackState; SigPauseStat acknowledges the applied toggle.
    return observer.wait([&](const Observations& state) {
        return state.pauseChanges > before.pauseChanges && state.paused == paused;
    }, paused ? "pause is applied and acknowledged" : "resume is applied and acknowledged");
}

bool StopVideo(PlaybackRuntime& runtime, PlaybackObserver& observer)
{
    const auto before = observer.snapshot();
    runtime.controller().stop();
    runtime.controller().stopAndWait();
    const auto state = observer.snapshot();
    return Expect(state.stops > before.stops, "explicit stop publishes Stopped") &&
           Expect(state.stopFinished == before.stopFinished + 1, "stopAndWait joins exactly one completed session") &&
           observer.expectQuiet("no video frames arrive after stopAndWait returns");
}

bool ExerciseVideo(const GeneratedMediaFixtures& fixtures)
{
    // Observer outlives runtime, including all callbacks during error-path destruction.
    PlaybackObserver observer;
    auto runtime = PlaybackRuntime::Create();
    if (!Expect(runtime != nullptr, "playback runtime initializes for integration playback"))
        return false;
    observer.connect(*runtime);
    if (!StartVideo(*runtime, observer, fixtures.video) ||
        !TogglePause(*runtime, observer, true) ||
        !observer.expectQuiet("acknowledged pause stops video frame delivery"))
        return false;

    // Seek while paused: a displayed bright frame cannot be natural playback advancement.
    // Check decoded content, not stale progress signals (VideoFrame exposes no PTS/serial).
    auto before = observer.snapshot();
    runtime->controller().seekSeconds(8);
    if (!observer.wait([&](const Observations& state) { return state.brightFrames > before.brightFrames; },
                       "paused forward seek delivers the target region's bright frame") ||
        !observer.expectQuiet("forward seek steps a frame without resuming playback"))
        return false;
    before = observer.snapshot();
    runtime->controller().seekSeconds(0);
    if (!observer.wait([&](const Observations& state) { return state.darkFrames > before.darkFrames; },
                       "paused rewind delivers a new opening-region dark frame") ||
        !observer.expectQuiet("rewind steps a frame without resuming playback"))
        return false;

    // Repeated acknowledged toggles exercise the real mailbox/refresh loop, not only command posting.
    for (int iteration = 0; iteration < 3; ++iteration) {
        before = observer.snapshot();
        if (!TogglePause(*runtime, observer, false) ||
            !observer.wait([&](const Observations& state) { return state.frames > before.frames; },
                           "resume produces a new delivered video frame") ||
            !TogglePause(*runtime, observer, true))
            return false;
    }
    if (!StopVideo(*runtime, observer) || !StartVideo(*runtime, observer, fixtures.video))
        return false;

    // No wait between stop and replay: play must retire the old session before opening the next.
    before = observer.snapshot();
    runtime->controller().stop();
    if (!StartVideo(*runtime, observer, fixtures.video))
        return false;
    const auto replayed = observer.snapshot();
    if (!Expect(replayed.stops > before.stops && replayed.stopFinished == before.stopFinished + 1 &&
                    replayed.lastStopFinishedEvent < replayed.lastOpeningEvent,
                "immediate replay completes the old session before the new Opening"))
        return false;

    // Destruction must join an actively delivering session while the observer remains alive.
    before = observer.snapshot();
    runtime.reset();
    return Expect(observer.snapshot().stopFinished == before.stopFinished + 1,
                  "active runtime destruction completes its playback session") &&
           observer.expectQuiet("no frame callbacks arrive after active runtime destruction");
}

bool ExerciseAudio(const GeneratedMediaFixtures& fixtures)
{
    PlaybackObserver observer;
    auto runtime = PlaybackRuntime::Create();
    if (!Expect(runtime != nullptr, "audio runtime initializes"))
        return false;
    observer.connect(*runtime);
    if (!Expect(runtime->controller().play(fixtures.wav.string()), "generated WAV opens") ||
        !observer.wait([](const Observations& state) { return state.mediaInfos > 0 && state.playings > 0; },
                       "generated WAV publishes metadata and enters Playing"))
        return false;
    const auto state = observer.snapshot();
    if (!Expect(!state.mediaInfo.live && state.mediaInfo.seekable &&
                    std::any_of(state.mediaInfo.tracks.begin(), state.mediaInfo.tracks.end(), [](const TrackInfo& track) {
                        return track.type == AVMEDIA_TYPE_AUDIO;
                    }), "generated WAV exposes an audio track"))
        return false;
    runtime->controller().stopAndWait();
    return Expect(observer.snapshot().stopFinished == 1, "audio stopAndWait completes its session");
}

} // namespace

int main()
{
    try {
        const FixtureDirectory directory;
        const GeneratedMediaFixtures fixtures = BuildMediaFixtures(directory.path, videoFrameCount);
        if (!Expect(std::filesystem::file_size(fixtures.wav) > 44, "PCM WAV fixture contains audio data") ||
            !Expect(std::filesystem::file_size(fixtures.video) > 0, "FFmpeg video fixture is written") ||
            !Expect(std::filesystem::file_size(fixtures.subtitle) > 0, "subtitle fixture is written"))
            return 1;
        AVFormatContext* format = nullptr;
        if (!Expect(avformat_open_input(&format, fixtures.video.string().c_str(), nullptr, nullptr) == 0,
                    "FFmpeg can reopen the generated video") ||
            !Expect(avformat_find_stream_info(format, nullptr) >= 0, "generated video has stream information") ||
            !Expect(format->duration > 0, "generated video has deterministic duration")) {
            avformat_close_input(&format);
            return 1;
        }
        avformat_close_input(&format);
        return ExerciseVideo(fixtures) && ExerciseAudio(fixtures) ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: playback integration exception: " << error.what() << '\n';
        return 1;
    }
}
