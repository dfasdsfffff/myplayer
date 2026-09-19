#define SDL_MAIN_HANDLED

#include "media_fixture_builder.h"
#include "playback_runtime.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <filesystem>
#include <iostream>
#include <chrono>
#include <condition_variable>
#include <mutex>

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
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "myplayer-media-fixtures";
    const GeneratedMediaFixtures fixtures = BuildMediaFixtures(directory);
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

    auto runtime = PlaybackRuntime::Create();
    if (!Expect(runtime != nullptr, "playback runtime initializes for integration playback"))
        return 1;
    std::mutex frameMutex;
    std::condition_variable frameReady;
    bool receivedFrame = false;
    bool receivedMediaInfo = false;
    MediaInfo mediaInfo;
    auto connection = runtime->SigVideoFrame.connect([&](std::shared_ptr<VideoFrame> frame) {
        if (!frame)
            return;
        std::lock_guard<std::mutex> lock(frameMutex);
        receivedFrame = true;
        frameReady.notify_one();
    });
    auto mediaInfoConnection = runtime->SigMediaInfo.connect([&](const MediaInfo& info) {
        std::lock_guard<std::mutex> lock(frameMutex);
        mediaInfo = info;
        receivedMediaInfo = true;
        frameReady.notify_one();
    });
    if (!Expect(runtime->controller().play(fixtures.video.string()), "generated video opens"))
        return 1;
    {
        std::unique_lock<std::mutex> lock(frameMutex);
        if (!Expect(frameReady.wait_for(lock, std::chrono::seconds(5), [&] { return receivedFrame; }),
                    "generated video produces a first frame")) {
            runtime->controller().stopAndWait();
            return 1;
        }
        if (!Expect(frameReady.wait_for(lock, std::chrono::seconds(5), [&] { return receivedMediaInfo; }),
                    "generated video publishes media information") ||
            !Expect(mediaInfo.seekable && mediaInfo.duration && mediaInfo.duration->count() >= 900 &&
                        mediaInfo.width == 64 && mediaInfo.height == 48,
                    "media information exposes generated video duration, seekability, and dimensions")) {
            runtime->controller().stopAndWait();
            return 1;
        }
    }
    runtime->controller().pause();
    runtime->controller().setSpeed(1.5);
    runtime->controller().seekSeconds(0);
    runtime->controller().pause();
    runtime->controller().stopAndWait();
    receivedFrame = false;
    if (!Expect(runtime->controller().play(fixtures.video.string()), "generated video replays after stop"))
        return 1;
    runtime->controller().stopAndWait();
    return 0;
}
