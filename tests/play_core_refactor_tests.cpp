#include "media_session.h"
#include "enums.h"
#include "playback_controller.h"
#include "renderer_dispatcher.h"

#include <iostream>
#include <memory>

namespace {

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

int CloseCount = 0;
VideoState* LastClosedState = nullptr;

void CountClose(VideoState* state)
{
    ++CloseCount;
    LastClosedState = state;
}

} // namespace

int main()
{
    auto* firstState = reinterpret_cast<VideoState*>(0x1);
    auto* secondState = reinterpret_cast<VideoState*>(0x2);

    {
        MediaSession session(firstState, CountClose);
        if (!Expect(session.get() == firstState, "session should expose current state"))
            return 1;

        session.reset(secondState);
        if (!Expect(CloseCount == 1, "reset should close previous state"))
            return 1;
        if (!Expect(LastClosedState == firstState, "reset should close the old state"))
            return 1;
        if (!Expect(session.get() == secondState, "reset should install the new state"))
            return 1;

        VideoState* released = session.release();
        if (!Expect(released == secondState, "release should return current state"))
            return 1;
        if (!Expect(session.get() == nullptr, "release should clear current state"))
            return 1;
    }

    if (!Expect(CloseCount == 1, "released state should not close in destructor"))
        return 1;

    bool dimensionsChanged = false;
    int dimensionWidth = 0;
    int dimensionHeight = 0;
    int frameCount = 0;

    RendererDispatcher dispatcher;
    dispatcher.setFrameDimensionsChangedCallback([&](int width, int height) {
        dimensionsChanged = true;
        dimensionWidth = width;
        dimensionHeight = height;
    });
    dispatcher.setVideoFrameCallback([&](std::shared_ptr<VideoFrame> frame) {
        if (frame)
            ++frameCount;
    });

    auto frame = std::make_shared<VideoFrame>();
    frame->width = 640;
    frame->height = 360;
    frame->bytesPerLine = 640 * 4;
    frame->bgra.resize(static_cast<size_t>(frame->bytesPerLine) * frame->height);

    dispatcher.dispatchFrame(frame);
    if (!Expect(dimensionsChanged, "first frame should publish dimensions"))
        return 1;
    if (!Expect(dimensionWidth == 640 && dimensionHeight == 360, "dimension callback should receive frame size"))
        return 1;
    if (!Expect(frameCount == 1, "valid frame should be dispatched"))
        return 1;

    dimensionsChanged = false;
    dispatcher.dispatchFrame(frame);
    if (!Expect(!dimensionsChanged, "same-size frame should not republish dimensions"))
        return 1;
    if (!Expect(frameCount == 2, "same-size frame should still be dispatched"))
        return 1;

    dispatcher.dispatchFrame(nullptr);
    if (!Expect(frameCount == 2, "null frame should be ignored"))
        return 1;

    std::string playedFile;
    int pauseCount = 0;
    int stopCount = 0;
    int stopAndWaitCount = 0;
    double lastSeekPercent = -1.0;
    int lastSeekSeconds = -1;
    int seekForwardCount = 0;
    int seekBackCount = 0;
    double lastVolumePercent = -1.0;
    double lastSpeed = -1.0;
    VideoLoopPolicy lastLoopPolicy = LOOP_NONE;
    int cycleAudioTrackCount = 0;
    int cycleSubtitleTrackCount = 0;
    int addVolumeCount = 0;
    int subVolumeCount = 0;

    PlaybackController controller({
        [&](const std::string& fileName) {
            playedFile = fileName;
            return true;
        },
        [&]() { ++pauseCount; },
        [&](double percent) { lastSeekPercent = percent; },
        [&](int seconds) { lastSeekSeconds = seconds; },
        [&]() { ++seekForwardCount; },
        [&]() { ++seekBackCount; },
        [&]() { ++stopCount; },
        [&]() { ++stopAndWaitCount; },
        [&](double percent) { lastVolumePercent = percent; },
        [&](double speed) { lastSpeed = speed; },
        [&](VideoLoopPolicy policy) { lastLoopPolicy = policy; },
        [&]() { ++cycleAudioTrackCount; },
        [&]() { ++cycleSubtitleTrackCount; },
        [&]() { ++addVolumeCount; },
        [&]() { ++subVolumeCount; },
    });

    if (!Expect(controller.play("movie.mp4"), "play should return engine play result"))
        return 1;
    if (!Expect(playedFile == "movie.mp4", "play should forward file name"))
        return 1;

    controller.pause();
    controller.seek(0.5);
    controller.seekSeconds(42);
    controller.seekForward();
    controller.seekBack();
    controller.stop();
    controller.stopAndWait();
    controller.setVolume(0.75);
    controller.setSpeed(1.5);
    controller.setLoopPolicy(LOOP_SINGLE);
    controller.cycleAudioTrack();
    controller.cycleSubtitleTrack();
    controller.addVolume();
    controller.subVolume();

    if (!Expect(pauseCount == 1, "pause should forward once"))
        return 1;
    if (!Expect(lastSeekPercent == 0.5, "seek should forward percent"))
        return 1;
    if (!Expect(lastSeekSeconds == 42, "seekSeconds should forward seconds"))
        return 1;
    if (!Expect(seekForwardCount == 1, "seekForward should forward once"))
        return 1;
    if (!Expect(seekBackCount == 1, "seekBack should forward once"))
        return 1;
    if (!Expect(stopCount == 1, "stop should forward once"))
        return 1;
    if (!Expect(stopAndWaitCount == 1, "stopAndWait should forward once"))
        return 1;
    if (!Expect(lastVolumePercent == 0.75, "setVolume should forward percent"))
        return 1;
    if (!Expect(lastSpeed == 1.5, "setSpeed should forward speed"))
        return 1;
    if (!Expect(lastLoopPolicy == LOOP_SINGLE, "setLoopPolicy should forward policy"))
        return 1;
    if (!Expect(cycleAudioTrackCount == 1, "cycleAudioTrack should forward once"))
        return 1;
    if (!Expect(cycleSubtitleTrackCount == 1, "cycleSubtitleTrack should forward once"))
        return 1;
    if (!Expect(addVolumeCount == 1, "addVolume should forward once"))
        return 1;
    if (!Expect(subVolumeCount == 1, "subVolume should forward once"))
        return 1;

    return 0;
}
