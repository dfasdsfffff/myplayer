#include "playback_controller.h"

#include "videoctl.h"

PlaybackController CreatePlaybackController(VideoCtl& ctl)
{
    return PlaybackController({
        [&ctl](const std::string& fileName) {
            return ctl.StartPlay(fileName);
        },
        [&ctl]() {
            ctl.OnPause();
        },
        [&ctl](double percent) {
            ctl.OnPlaySeek(percent);
        },
        [&ctl](int seconds) {
            ctl.OnPlaySeekSeconds(seconds);
        },
        [&ctl]() {
            ctl.OnSeekForward();
        },
        [&ctl]() {
            ctl.OnSeekBack();
        },
        [&ctl]() {
            ctl.OnStop();
        },
        [&ctl]() {
            ctl.OnStopAndWait();
        },
        [&ctl](double percent) {
            ctl.OnPlayVolume(percent);
        },
        [&ctl](double speed) {
            ctl.set_play_speed(speed);
        },
        [&ctl](VideoLoopPolicy policy) {
            ctl.set_play_loop_policy(policy);
        },
        [&ctl]() {
            ctl.OnCycleAudioTrack();
        },
        [&ctl]() {
            ctl.OnCycleSubtitleTrack();
        },
        [&ctl]() {
            ctl.OnAddVolume();
        },
        [&ctl]() {
            ctl.OnSubVolume();
        },
    });
}
