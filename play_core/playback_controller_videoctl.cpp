#include "playback_controller.h"

#include "videoctl.h"

namespace {

VideoCtl* PlaybackEngine()
{
    return VideoCtl::GetInstance();
}

} // namespace

PlaybackController* PlaybackController::GetInstance()
{
    static PlaybackController instance({
        [](const std::string& fileName) {
            VideoCtl* ctl = PlaybackEngine();
            return ctl ? ctl->StartPlay(fileName) : false;
        },
        []() {
            if (VideoCtl* ctl = PlaybackEngine())
                ctl->OnPause();
        },
        [](double percent) {
            if (VideoCtl* ctl = PlaybackEngine())
                ctl->OnPlaySeek(percent);
        },
        [](int seconds) {
            if (VideoCtl* ctl = PlaybackEngine())
                ctl->OnPlaySeekSeconds(seconds);
        },
        []() {
            if (VideoCtl* ctl = PlaybackEngine())
                ctl->OnSeekForward();
        },
        []() {
            if (VideoCtl* ctl = PlaybackEngine())
                ctl->OnSeekBack();
        },
        []() {
            if (VideoCtl* ctl = PlaybackEngine())
                ctl->OnStop();
        },
        []() {
            if (VideoCtl* ctl = PlaybackEngine())
                ctl->OnStopAndWait();
        },
        [](double percent) {
            if (VideoCtl* ctl = PlaybackEngine())
                ctl->OnPlayVolume(percent);
        },
        [](double speed) {
            if (VideoCtl* ctl = PlaybackEngine())
                ctl->set_play_speed(speed);
        },
        [](VideoLoopPolicy policy) {
            if (VideoCtl* ctl = PlaybackEngine())
                ctl->set_play_loop_policy(policy);
        },
        []() {
            if (VideoCtl* ctl = PlaybackEngine())
                ctl->OnCycleAudioTrack();
        },
        []() {
            if (VideoCtl* ctl = PlaybackEngine())
                ctl->OnCycleSubtitleTrack();
        },
        []() {
            if (VideoCtl* ctl = PlaybackEngine())
                ctl->OnAddVolume();
        },
        []() {
            if (VideoCtl* ctl = PlaybackEngine())
                ctl->OnSubVolume();
        },
    });
    return &instance;
}
