#include "playback_controller_videoctl.h"

#include "videoctl.h"

PlaybackController CreatePlaybackController(VideoCtl& ctl)
{
    return PlaybackController({
        [&ctl](const MediaSource& source) {
            return ctl.StartPlay(source);
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
		[&ctl](HardwareDecodePreference preference) {
			ctl.set_hardware_decode_preference(preference);
		},
        [&ctl]() {
            ctl.OnCycleAudioTrack();
        },
        [&ctl]() {
            ctl.OnCycleSubtitleTrack();
        },
        [&ctl](int streamIndex) {
            ctl.OnSelectAudioTrack(streamIndex);
        },
        [&ctl](std::optional<int> streamIndex) {
            ctl.OnSelectSubtitleTrack(streamIndex);
        },
        [&ctl]() {
            ctl.OnAddVolume();
        },
        [&ctl]() {
            ctl.OnSubVolume();
        },
    });
}
