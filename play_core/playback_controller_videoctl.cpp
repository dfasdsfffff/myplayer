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
    });
    return &instance;
}
