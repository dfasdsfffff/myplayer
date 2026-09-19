#define SDL_MAIN_HANDLED

#include "media_session.h"
#include "enums.h"
#include "media_source.h"
#include "media_sync.h"
#include "playback_controller.h"
#include "playback_controller_videoctl.h"
#include "playback_runtime.h"
#include "playback_settings.h"
#include "renderer_dispatcher.h"
#include "videoctl.h"
#include "video_frame_converter.h"
#include "video_state.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <iterator>
#include <memory>
#include <type_traits>

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

struct AvFrameDeleter {
    void operator()(AVFrame* frame) const
    {
        av_frame_free(&frame);
    }
};

} // namespace

int main()
{
    static_assert(std::is_same_v<decltype(static_cast<bool (VideoCtl::*)(const std::string&)>(&VideoCtl::StartPlay)),
        bool (VideoCtl::*)(const std::string&)>,
        "VideoCtl::StartPlay(string) public signature must remain stable");
    static_assert(std::is_same_v<decltype(static_cast<bool (VideoCtl::*)(const MediaSource&)>(&VideoCtl::StartPlay)),
        bool (VideoCtl::*)(const MediaSource&)>,
        "VideoCtl::StartPlay(MediaSource) public signature must remain stable");
    static_assert(std::is_same_v<decltype(&VideoCtl::OnStop), void (VideoCtl::*)()>,
        "VideoCtl::OnStop public signature must remain stable");
    static_assert(std::is_same_v<decltype(&VideoCtl::OnStopAndWait), void (VideoCtl::*)()>,
        "VideoCtl::OnStopAndWait public signature must remain stable");
    static_assert(std::is_same_v<decltype(&VideoCtl::OnPlaySeek), void (VideoCtl::*)(double)>,
        "VideoCtl::OnPlaySeek public signature must remain stable");
    static_assert(std::is_same_v<decltype(&VideoCtl::OnPlaySeekSeconds), void (VideoCtl::*)(int)>,
        "VideoCtl::OnPlaySeekSeconds public signature must remain stable");
    static_assert(std::is_same_v<decltype(&VideoCtl::OnSeekForward), void (VideoCtl::*)()>,
        "VideoCtl::OnSeekForward public signature must remain stable");
    static_assert(std::is_same_v<decltype(&VideoCtl::OnSeekBack), void (VideoCtl::*)()>,
        "VideoCtl::OnSeekBack public signature must remain stable");
    static_assert(std::is_same_v<decltype(&VideoCtl::OnPause), void (VideoCtl::*)()>,
        "VideoCtl::OnPause public signature must remain stable");
    static_assert(std::is_same_v<decltype(&VideoCtl::OnPlayVolume), void (VideoCtl::*)(double)>,
        "VideoCtl::OnPlayVolume public signature must remain stable");
    static_assert(std::is_same_v<decltype(&VideoCtl::set_play_speed), void (VideoCtl::*)(double)>,
        "VideoCtl::set_play_speed public signature must remain stable");
    static_assert(std::is_same_v<decltype(&VideoCtl::set_play_loop_policy), void (VideoCtl::*)(VideoLoopPolicy)>,
        "VideoCtl::set_play_loop_policy public signature must remain stable");
    static_assert(std::is_same_v<decltype(&VideoCtl::OnCycleAudioTrack), void (VideoCtl::*)()>,
        "VideoCtl::OnCycleAudioTrack public signature must remain stable");
    static_assert(std::is_same_v<decltype(&VideoCtl::OnCycleSubtitleTrack), void (VideoCtl::*)()>,
        "VideoCtl::OnCycleSubtitleTrack public signature must remain stable");

    VideoFrameConverter frameConverter;
    if (!Expect(frameConverter.convert(nullptr) == nullptr, "converter should reject a missing source frame"))
        return 1;

    std::unique_ptr<AVFrame, AvFrameDeleter> sourceFrame(av_frame_alloc());
    if (!Expect(sourceFrame != nullptr, "test source frame should allocate"))
        return 1;
    sourceFrame->format = AV_PIX_FMT_BGRA;
    sourceFrame->width = 2;
    sourceFrame->height = 2;
    if (!Expect(av_frame_get_buffer(sourceFrame.get(), 32) == 0, "test source buffer should allocate"))
        return 1;

    for (int row = 0; row < sourceFrame->height; ++row)
        std::fill_n(sourceFrame->data[0] + row * sourceFrame->linesize[0], sourceFrame->width * 4, uint8_t{0});
    const uint8_t firstPixel[] = {17, 34, 51, 255};
    std::copy(std::begin(firstPixel), std::end(firstPixel), sourceFrame->data[0]);

    const auto convertedFrame = frameConverter.convert(sourceFrame.get());
    if (!Expect(convertedFrame != nullptr, "converter should produce a display frame"))
        return 1;
    if (!Expect(convertedFrame->width == 2 && convertedFrame->height == 2,
            "converted frame should preserve source dimensions"))
        return 1;
    if (!Expect(convertedFrame->bytesPerLine == 8 && convertedFrame->bgra.size() == 16,
            "converted frame should expose tightly packed BGRA storage"))
        return 1;
    if (!Expect(std::equal(std::begin(firstPixel), std::end(firstPixel), convertedFrame->bgra.begin()),
            "BGRA input should preserve pixel values"))
        return 1;

    static_assert(std::is_same_v<decltype(std::declval<PacketQueue&>().serialStorage()), std::atomic<int>*>,
        "packet queue generation storage must remain safe to observe across threads");

    std::atomic<int> queueSerial{1};
    Clock queueClock;
    queueClock.init(&queueSerial);
    queueClock.set(12.5, 1);
    if (!Expect(queueClock.get() == 12.5, "clock should remain valid for the current queue generation"))
        return 1;
    queueSerial.fetch_add(1, std::memory_order_release);
    if (!Expect(std::isnan(queueClock.get()), "clock should become obsolete after the queue generation changes"))
        return 1;

    if (!Expect(PlaybackSettings::NormalizeVolume(-0.5) == 0.0, "negative volume should clamp to zero"))
        return 1;
    if (!Expect(PlaybackSettings::NormalizeVolume(1.5) == 1.0, "volume above one should clamp to one"))
        return 1;
    if (!Expect(PlaybackSettings::ToSdlVolume(0.30, 128) == 38, "normalized volume should convert once to SDL units"))
        return 1;
    if (!Expect(PlaybackSettings::ToSdlVolume(0.30, 128) == 38, "repeated conversion should not drift"))
        return 1;

    static_assert(std::is_same_v<decltype(CreatePlaybackController), PlaybackController(VideoCtl&)>,
        "PlaybackController should be creatable from an explicit VideoCtl instance");
    static_assert(std::is_same_v<decltype(PlaybackRuntime::Create()), std::unique_ptr<PlaybackRuntime>>,
        "PlaybackRuntime should own playback internals behind a factory");
    static_assert(std::is_same_v<decltype(std::declval<PlaybackRuntime&>().controller()), PlaybackController&>,
        "PlaybackRuntime should expose a controller without exposing VideoCtl");
    static_assert(std::is_same_v<decltype(std::declval<VideoState&>().session), SessionState>,
        "VideoState should expose grouped session state");
    static_assert(std::is_same_v<decltype(std::declval<VideoState&>().clocks), MediaClockState>,
        "VideoState should expose grouped clock state");
    static_assert(std::is_same_v<decltype(std::declval<VideoState&>().audio), AudioState>,
        "VideoState should expose grouped audio state");
    static_assert(std::is_same_v<decltype(std::declval<VideoState&>().video), VideoTrackState>,
        "VideoState should expose grouped video state");
    static_assert(std::is_same_v<decltype(std::declval<VideoState&>().subtitle), SubtitleState>,
        "VideoState should expose grouped subtitle state");
    static_assert(std::is_same_v<decltype(std::declval<VideoState&>().filters), FilterState>,
        "VideoState should expose grouped filter state");

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

    {
        auto syncState = std::make_unique<VideoState>();
        syncState->clocks.av_sync_type = AV_SYNC_AUDIO_MASTER;
    syncState->clocks.extclk.init(syncState->clocks.extclk.serialStorage());
        syncState->video.max_frame_duration = 10.0;
        if (!Expect(MediaSync::get_master_sync_type(syncState.get()) == AV_SYNC_EXTERNAL_CLOCK,
                "missing audio stream should fall back to external clock"))
            return 1;
        if (!Expect(std::isnan(MediaSync::get_master_clock(syncState.get())),
                "default external clock should report NaN until playback initializes it"))
            return 1;

        Frame current{};
        Frame next{};
        current.serial = 1;
        current.pts = 1.0;
        current.duration = 0.04;
        next.serial = 1;
        next.pts = 1.04;
        if (!Expect(MediaSync::vp_duration(syncState.get(), &current, &next) > 0.0,
                "frame duration helper should use grouped video state"))
            return 1;
    }

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

    MediaSource playedSource;
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
    HardwareDecodePreference lastHardwareDecodePreference = HardwareDecodePreference::Disabled;
    int cycleAudioTrackCount = 0;
    int cycleSubtitleTrackCount = 0;
    int selectedAudioTrack = -1;
    std::optional<int> selectedSubtitleTrack = 0;
    int selectSubtitleTrackCount = 0;
    int addVolumeCount = 0;
    int subVolumeCount = 0;

    PlaybackController controller({
        [&](const MediaSource& source) {
            playedSource = source;
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
        [&](HardwareDecodePreference preference) { lastHardwareDecodePreference = preference; },
        [&]() { ++cycleAudioTrackCount; },
        [&]() { ++cycleSubtitleTrackCount; },
        [&](int streamIndex) { selectedAudioTrack = streamIndex; },
        [&](std::optional<int> streamIndex) {
            selectedSubtitleTrack = streamIndex;
            ++selectSubtitleTrackCount;
        },
        [&]() { ++addVolumeCount; },
        [&]() { ++subVolumeCount; },
    });

    if (!Expect(controller.play("movie.mp4"), "play should return engine play result"))
        return 1;
    if (!Expect(playedSource.location == "movie.mp4", "string play should wrap source"))
        return 1;

    MediaSource source{"rtsp://host/live"};
    source.network.maxReconnectAttempts = 3;
    if (!Expect(controller.play(source), "structured play should return engine play result"))
        return 1;
    if (!Expect(playedSource.network.maxReconnectAttempts == 3, "structured options should forward"))
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
    controller.setHardwareDecodePreference(HardwareDecodePreference::D3D11VA);
    controller.cycleAudioTrack();
    controller.cycleSubtitleTrack();
    controller.selectAudioTrack(4);
    controller.selectSubtitleTrack(7);
    controller.selectSubtitleTrack(std::nullopt);
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
    if (!Expect(lastHardwareDecodePreference == HardwareDecodePreference::D3D11VA,
                "setHardwareDecodePreference should forward preference"))
        return 1;
    if (!Expect(cycleAudioTrackCount == 1, "cycleAudioTrack should forward once"))
        return 1;
    if (!Expect(cycleSubtitleTrackCount == 1, "cycleSubtitleTrack should forward once"))
        return 1;
    if (!Expect(selectedAudioTrack == 4, "selectAudioTrack should forward the stream index"))
        return 1;
    if (!Expect(selectSubtitleTrackCount == 2 && !selectedSubtitleTrack,
                "selectSubtitleTrack should forward selection and subtitle disable"))
        return 1;
    if (!Expect(addVolumeCount == 1, "addVolume should forward once"))
        return 1;
    if (!Expect(subVolumeCount == 1, "subVolume should forward once"))
        return 1;

    return 0;
}
