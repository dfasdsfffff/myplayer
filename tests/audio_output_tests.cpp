#define SDL_MAIN_HANDLED

#include "audio_output.h"

#include "av_constants.h"
#include "av_types.h"
#include "video_state.h"

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

std::unique_ptr<VideoState> MakeState()
{
    auto state = std::make_unique<VideoState>();
    state->video.videoq.init();
    state->audio.audioq.init();
    state->subtitle.subtitleq.init();
    state->clocks.vidclk.init(&state->video.videoq.serial);
    state->clocks.audclk.init(&state->audio.audioq.serial);
    state->clocks.extclk.init(&state->clocks.extclk.serial);
    state->audio.audio_src.freq = 48000;
    state->audio.audio_diff_threshold = 0.001;
    state->audio.audio_diff_avg_coef = 0.0;
    state->audio.audio_diff_avg_count = AUDIO_DIFF_AVG_NB;
    return state;
}

} // namespace

int main()
{
    {
        auto state = MakeState();
        state->clocks.av_sync_type = AV_SYNC_AUDIO_MASTER;
        state->audio.audio_st = reinterpret_cast<AVStream*>(1);
        if (!Expect(AudioOutput::SynchronizeSamples(state.get(), 1000) == 1000,
                "audio master leaves sample count unchanged"))
            return 1;
    }

    {
        auto state = MakeState();
        state->clocks.av_sync_type = AV_SYNC_EXTERNAL_CLOCK;
        state->clocks.audclk.set(NAN, 0);
        state->clocks.extclk.set(0.0, 0);
        if (!Expect(AudioOutput::SynchronizeSamples(state.get(), 1000) == 1000,
                "invalid audio clock leaves sample count unchanged"))
            return 1;
    }

    {
        auto state = MakeState();
        state->clocks.av_sync_type = AV_SYNC_EXTERNAL_CLOCK;
        state->clocks.audclk.set(1.0, 0);
        state->clocks.extclk.set(0.0, 0);
        if (!Expect(AudioOutput::SynchronizeSamples(state.get(), 1000) == 1100,
                "positive bounded A/V difference clamps sample count upward"))
            return 1;

        state = MakeState();
        state->clocks.av_sync_type = AV_SYNC_EXTERNAL_CLOCK;
        state->clocks.audclk.set(-1.0, 0);
        state->clocks.extclk.set(0.0, 0);
        if (!Expect(AudioOutput::SynchronizeSamples(state.get(), 1000) == 900,
                "negative bounded A/V difference clamps sample count downward"))
            return 1;
    }

    {
        SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy");
        if (!Expect(SDL_InitSubSystem(SDL_INIT_AUDIO) == 0, "SDL audio subsystem should initialize"))
            return 1;
        AudioOutput output;
        if (!Expect(output.DeviceId() == 0, "default output has no open device"))
            return 1;
        output.Close();
        output.Close();
        if (!Expect(output.DeviceId() == 0, "closing default output twice leaves no device"))
            return 1;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    return 0;
}
