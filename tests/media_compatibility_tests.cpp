#define SDL_MAIN_HANDLED

#include "media_fixture_builder.h"
#include "playback_runtime.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <utility>

namespace {
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

// Includes shutdown: a broken join must fail the test rather than hang CTest.
class Deadline final {
public:
    Deadline() : worker([this] {
        std::unique_lock lock(mutex);
        if (!changed.wait_for(lock, 25s, [this] { return finished; })) {
            std::cerr << "FAILED: runtime compatibility case exceeded 25-second deadline\n";
            std::_Exit(1);
        }
    }) {}
    ~Deadline()
    {
        { std::lock_guard lock(mutex); finished = true; }
        changed.notify_all();
        worker.join();
    }
private:
    std::mutex mutex;
    std::condition_variable changed;
    bool finished{false};
    std::thread worker;
};

class FixtureDirectory final {
public:
    FixtureDirectory()
    {
        const auto stamp = Clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            auto candidate = std::filesystem::temp_directory_path() /
                ("myplayer-compatibility-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(candidate)) { path = candidate; return; }
        }
        throw std::runtime_error("cannot reserve isolated compatibility fixtures");
    }
    ~FixtureDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        if (error) std::cerr << "Cannot remove compatibility fixtures: " << error.message() << '\n';
    }
    std::filesystem::path path;
};

std::string Json(const std::string& value)
{
    constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (unsigned char character : value) {
        if (character == '"' || character == '\\') { result += '\\'; result += character; }
        else if (character < 0x20) {
            result += "\\u00"; result += hex[character >> 4]; result += hex[character & 15];
        } else result += character;
    }
    return result + '"';
}

const char* StateName(PlaybackState state)
{
    switch (state) {
    case PlaybackState::Opening: return "opening";
    case PlaybackState::Playing: return "playing";
    case PlaybackState::Stopped: return "stopped";
    case PlaybackState::Failed: return "failed";
    case PlaybackState::Buffering: return "buffering";
    case PlaybackState::Reconnecting: return "reconnecting";
    }
    return "unknown";
}
const char* ErrorName(PlaybackError error)
{
    switch (error) {
    case PlaybackError::None: return "none";
    case PlaybackError::Cancelled: return "cancelled";
    case PlaybackError::Timeout: return "timeout";
    case PlaybackError::Authentication: return "authentication";
    case PlaybackError::NotFound: return "not_found";
    case PlaybackError::UnsupportedProtocol: return "unsupported_protocol";
    case PlaybackError::NetworkUnavailable: return "network_unavailable";
    case PlaybackError::ConnectionLost: return "connection_lost";
    case PlaybackError::InvalidMedia: return "invalid_media";
    case PlaybackError::DecoderFailure: return "decoder_failure";
    case PlaybackError::Unknown: return "unknown";
    }
    return "unknown";
}

bool ValidFrame(const VideoFrame& frame)
{
    if (frame.width <= 0 || frame.height <= 0) return false;
    if (frame.format == VideoFrameFormat::Bgra32)
        return frame.bytesPerLine >= static_cast<std::int64_t>(frame.width) * 4 &&
            frame.bgra.size() >= static_cast<std::size_t>(frame.bytesPerLine) * frame.height;
    if (frame.format != VideoFrameFormat::Yuv420P) return false;
    for (int index = 0; index < 3; ++index) {
        const int width = index ? (frame.width + 1) / 2 : frame.width;
        const int height = index ? (frame.height + 1) / 2 : frame.height;
        if (!frame.planes[index].data || frame.planes[index].stride < width ||
            frame.planes[index].height < height) return false;
    }
    return true;
}

struct Observation {
    MediaInfo info;
    std::vector<PlaybackStatus> statuses;
    int metadataEvents{0};
    int frames{0};
    int invalidFrames{0};
    int width{0};
    int height{0};
    int dimensionChanges{0};
    int darkFrames{0};
    int brightFrames{0};
    int completions{0};
    int stopFinished{0};
    bool failed{false};
};

class Observer final {
public:
    void connect(PlaybackRuntime& runtime)
    {
        connections.emplace_back(runtime.SigMediaInfo.connect([this](const MediaInfo& info) {
            std::lock_guard lock(mutex); value.info = info; ++value.metadataEvents; changed.notify_all();
        }));
        connections.emplace_back(runtime.SigPlaybackStatus.connect([this](const PlaybackStatus& status) {
            std::lock_guard lock(mutex);
            value.statuses.push_back(status);
            value.failed |= status.state == PlaybackState::Failed;
            changed.notify_all();
        }));
        connections.emplace_back(runtime.SigVideoFrame.connect([this](const std::shared_ptr<VideoFrame>& frame) {
            std::lock_guard lock(mutex);
            ++value.frames;
            if (!frame || !ValidFrame(*frame)) { ++value.invalidFrames; changed.notify_all(); return; }
            if (value.width && (value.width != frame->width || value.height != frame->height))
                ++value.dimensionChanges;
            value.width = frame->width; value.height = frame->height;
            if (frame->width == 64 && frame->height == 48) {
                if (frame->format == VideoFrameFormat::Yuv420P) {
                    const int y = frame->planes[0].data.get()[24 * frame->planes[0].stride + 32];
                    value.darkFrames += y < 100; value.brightFrames += y > 150;
                } else {
                    const int green = frame->bgra[24 * frame->bytesPerLine + 32 * 4 + 1];
                    value.darkFrames += green < 40; value.brightFrames += green > 100;
                }
            }
            changed.notify_all();
        }));
        connections.emplace_back(runtime.SigPlayNextOne.connect([this] {
            std::lock_guard lock(mutex); ++value.completions; changed.notify_all();
        }));
        connections.emplace_back(runtime.SigStopFinished.connect([this] {
            std::lock_guard lock(mutex); ++value.stopFinished; changed.notify_all();
        }));
    }
    bool wait()
    {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 15s, [this] { return value.failed || value.completions > 0; });
    }
    bool waitForSecondLoop()
    {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 8s, [this] {
            return value.failed || (value.darkFrames >= 12 && value.brightFrames >= 12);
        }) && !value.failed;
    }
    Observation snapshot() { std::lock_guard lock(mutex); return value; }
private:
    std::mutex mutex;
    std::condition_variable changed;
    Observation value;
    std::vector<sigslot::scoped_connection> connections;
};

struct CaseResult {
    std::string media;
    std::string expected;
    std::string hardware;
    bool accepted{false};
    bool terminalObserved{false};
    bool naturalCompletion{false};
    double terminalMs{0};
    Observation observed;
    std::vector<std::string> failures;
    bool passed() const { return failures.empty(); }
};

CaseResult Run(const std::string& media, const std::string& expected, const std::string& hardware,
               bool generated = false)
{
    Deadline deadline;
    // Observer remains alive through stop, runtime destruction, and late callbacks.
    Observer observer;
    CaseResult result;
    result.media = media; result.expected = expected; result.hardware = hardware;
    auto runtime = PlaybackRuntime::Create();
    if (!runtime) { result.failures.emplace_back("runtime creation failed"); return result; }
    observer.connect(*runtime);
    runtime->controller().setLoopPolicy(LOOP_ALL); // EOF emits next-item without automatically replaying.
    runtime->controller().setHardwareDecodePreference(hardware == "disabled" ? HardwareDecodePreference::Disabled :
        hardware == "d3d11va" ? HardwareDecodePreference::D3D11VA : HardwareDecodePreference::Auto);
    const auto start = Clock::now();
    result.accepted = runtime->controller().play(media);
    result.terminalObserved = observer.wait();
    result.terminalMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    const auto terminal = observer.snapshot();
    result.naturalCompletion = terminal.completions == 1 && !terminal.failed;
    runtime->controller().stopAndWait();
    const auto joined = observer.snapshot();
    runtime.reset();
    result.observed = observer.snapshot();
    auto require = [&](bool condition, const char* failure) {
        if (!condition) result.failures.emplace_back(failure);
    };
    require(result.terminalObserved, "no EOF/failure within 15 seconds");
    require(result.observed.frames == joined.frames, "frames arrived after stopAndWait");
    require(result.observed.stopFinished == 1, "session did not finish exactly once");
    require(!result.observed.statuses.empty() && result.observed.statuses.front().state == PlaybackState::Opening,
            "Opening was not the first status");
    const bool playing = std::any_of(terminal.statuses.begin(), terminal.statuses.end(), [](const auto& status) {
        return status.state == PlaybackState::Playing;
    });
    const int audios = static_cast<int>(std::count_if(terminal.info.tracks.begin(), terminal.info.tracks.end(),
        [](const auto& track) { return track.type == AVMEDIA_TYPE_AUDIO; }));
    if (expected == "invalid") {
        require(terminal.failed, "invalid input did not publish Failed");
        require(!playing && terminal.frames == 0 && terminal.completions == 0,
                "invalid input played or completed as valid media");
        require(std::any_of(terminal.statuses.begin(), terminal.statuses.end(), [](const auto& status) {
            return status.state == PlaybackState::Failed && status.error == PlaybackError::InvalidMedia;
        }), "invalid input was not categorized InvalidMedia");
    } else {
        const bool wantVideo = expected != "audio";
        const bool wantAudio = expected != "video";
        require(result.accepted && playing && !terminal.failed, "valid input did not enter Playing without failure");
        require(result.naturalCompletion, "valid finite input did not naturally complete exactly once");
        require(result.observed.completions == 1, "duplicate natural completion arrived during shutdown");
        require(audios == (wantAudio ? 1 : 0), "actual audio tracks differ from expectation");
        require(!terminal.info.networkSource && !terminal.info.live && terminal.info.seekable &&
                terminal.info.duration && terminal.info.duration->count() > 0, "incorrect finite local-media metadata");
        require(std::all_of(terminal.info.tracks.begin(), terminal.info.tracks.end(), [](const auto& track) {
            return track.streamIndex >= 0 && !track.codec.empty();
        }), "track index or codec missing");
        require(terminal.invalidFrames == 0 && terminal.dimensionChanges == 0, "invalid frame storage or changing frame dimensions");
        if (wantVideo) {
            require(terminal.frames > 0 && terminal.width == terminal.info.width && terminal.height == terminal.info.height &&
                    terminal.width > 0 && terminal.height > 0, "video frames missing or disagree with metadata dimensions");
        } else {
            require(terminal.frames == 0 && terminal.info.width == 0 && terminal.info.height == 0,
                    "audio-only media published video or video dimensions");
        }
        if (terminal.info.duration)
            require(result.terminalMs + 250 >= terminal.info.duration->count(), "EOF before finite media could be consumed");
        if (generated && wantVideo) {
            require(terminal.width == 64 && terminal.height == 48 && terminal.darkFrames > 0 && terminal.brightFrames > 0,
                    "generated video dimensions/content regression");
            require(terminal.info.duration && terminal.info.duration->count() >= 1100 && terminal.info.duration->count() <= 1300,
                    "generated MPEG4 duration regression");
        }
        if (generated && !wantVideo)
            require(terminal.info.duration && terminal.info.duration->count() == 500, "generated WAV duration regression");
    }
    return result;
}

void Report(std::ostream& output, const CaseResult& result)
{
    const auto& observed = result.observed;
    output << "{\"schema_version\":1,\"runner\":\"playback_runtime_compatibility\",\"outcome\":"
           << Json(result.passed() ? "pass" : "fail") << ",\"media\":" << Json(result.media)
           << ",\"expected\":" << Json(result.expected) << ",\"requested_hardware\":" << Json(result.hardware)
           << ",\"linked_ffmpeg_version\":" << Json(av_version_info())
           << ",\"hardware_observation\":\"not exposed by runtime; see separate direct-context evidence\""
           << ",\"audio_observation\":\"track metadata, Playing, finite timed EOF; not acoustic/sample capture\""
           << ",\"play_accepted\":" << (result.accepted ? "true" : "false")
           << ",\"terminal_observed\":" << (result.terminalObserved ? "true" : "false")
           << ",\"natural_completion\":" << (result.naturalCompletion ? "true" : "false")
           << ",\"terminal_elapsed_ms\":" << result.terminalMs
           << ",\"completion_events\":" << observed.completions << ",\"stop_finished_events\":" << observed.stopFinished
           << ",\"video_frames\":" << observed.frames << ",\"invalid_frames\":" << observed.invalidFrames
           << ",\"frame_width\":" << observed.width << ",\"frame_height\":" << observed.height
           << ",\"metadata\":{\"events\":" << observed.metadataEvents << ",\"width\":" << observed.info.width
           << ",\"height\":" << observed.info.height << ",\"duration_ms\":";
    if (observed.info.duration) output << observed.info.duration->count(); else output << "null";
    output << ",\"seekable\":" << (observed.info.seekable ? "true" : "false")
           << ",\"live\":" << (observed.info.live ? "true" : "false") << ",\"tracks\":[";
    bool first = true;
    for (const auto& track : observed.info.tracks) {
        if (!first) output << ','; first = false;
        output << "{\"stream_index\":" << track.streamIndex << ",\"type\":"
               << Json(track.type == AVMEDIA_TYPE_VIDEO ? "video" : track.type == AVMEDIA_TYPE_AUDIO ? "audio" : "other")
               << ",\"codec\":" << Json(track.codec) << '}';
    }
    output << "]},\"statuses\":["; first = true;
    for (const auto& status : observed.statuses) {
        if (!first) output << ','; first = false;
        output << "{\"state\":" << Json(StateName(status.state)) << ",\"error\":" << Json(ErrorName(status.error))
               << ",\"message\":" << Json(status.message) << '}';
    }
    output << "],\"failures\":["; first = true;
    for (const auto& failure : result.failures) {
        if (!first) output << ','; first = false; output << Json(failure);
    }
    output << "]}\n";
}

} // namespace

int main(int argc, char** argv)
{
    std::string media, expected, hardware, report;
    if (argc != 1) {
        if (argc != 9) { std::cerr << "Usage: --media PATH --expect video|audio|av|invalid --hardware disabled|auto|d3d11va --report PATH\n"; return 2; }
        for (int index = 1; index < argc; index += 2) {
            const std::string option = argv[index];
            std::string* target = option == "--media" ? &media : option == "--expect" ? &expected :
                option == "--hardware" ? &hardware : option == "--report" ? &report : nullptr;
            if (!target || !target->empty() || !*argv[index + 1]) { std::cerr << "Invalid/duplicate option\n"; return 2; }
            *target = argv[index + 1];
        }
        if (media.empty() || report.empty() || (expected != "video" && expected != "audio" && expected != "av" && expected != "invalid") ||
            (hardware != "disabled" && hardware != "auto" && hardware != "d3d11va")) { std::cerr << "Invalid arguments\n"; return 2; }
    }
    try {
        if (argc == 1) {
            FixtureDirectory directory;
            const auto fixtures = BuildMediaFixtures(directory.path, 12);
            const auto invalid = directory.path / "invalid.bin";
            { std::ofstream file(invalid, std::ios::binary); file << "This is not a media container.\n"; }
            // Keep only RIFF signature/size, not enough to describe any AVI track.
            // This tests a precise unusable boundary, not the false premise that all truncation is invalid.
            const auto truncated = directory.path / "header-only.avi";
            { std::ifstream input(fixtures.video, std::ios::binary); char prefix[8]{}; input.read(prefix, sizeof(prefix));
              if (input.gcount() != sizeof(prefix)) throw std::runtime_error("cannot read AVI prefix");
              std::ofstream output(truncated, std::ios::binary); output.write(prefix, sizeof(prefix)); }
            bool passed = true;
            for (const auto& item : std::vector<std::pair<std::filesystem::path, std::string>>{
                     {fixtures.video, "video"}, {fixtures.wav, "audio"}, {invalid, "invalid"}, {truncated, "invalid"}}) {
                const auto result = Run(item.first.string(), item.second, "disabled", true);
                Report(std::cout, result); passed &= result.passed();
            }
            {
                Deadline deadline;
                Observer observer;
                auto runtime = PlaybackRuntime::Create();
                if (!runtime) throw std::runtime_error("loop regression runtime creation failed");
                observer.connect(*runtime);
                runtime->controller().setHardwareDecodePreference(HardwareDecodePreference::Disabled);
                runtime->controller().setLoopPolicy(LOOP_SINGLE);
                const bool opened = runtime->controller().play(fixtures.video.string());
                const bool repeated = opened && observer.waitForSecondLoop();
                runtime->controller().stopAndWait();
                const auto state = observer.snapshot();
                const bool loopPassed = repeated && state.completions == 0 && state.stopFinished == 1 &&
                    state.invalidFrames == 0;
                if (!loopPassed)
                    std::cerr << "FAILED: single-item repeat must seek through EOF and remain stoppable\n";
                std::cout << "{\"case\":\"single_item_repeat\",\"outcome\":\""
                          << (loopPassed ? "pass" : "fail") << "\"}\n";
                passed &= loopPassed;
            }
            return passed ? 0 : 1;
        }
        std::ofstream output(report, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot create JSON report");
        const auto result = Run(media, expected, hardware);
        Report(output, result); output.flush();
        if (!output) throw std::runtime_error("cannot write JSON report");
        return result.passed() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        if (!report.empty()) {
            std::ofstream output(report, std::ios::binary | std::ios::trunc);
            if (output)
                output << "{\"schema_version\":1,\"runner\":\"playback_runtime_compatibility\","
                       << "\"outcome\":\"fail\",\"observations\":null,\"error\":" << Json(error.what()) << "}\n";
        }
        return 1;
    }
}
