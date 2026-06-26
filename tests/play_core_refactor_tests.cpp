#include "media_session.h"
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

    return 0;
}
