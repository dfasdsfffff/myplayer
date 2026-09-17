#pragma once

#include "av_types.h"

struct VideoState;

class AudioOutput final {
public:
    AudioOutput() = default;
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    int Open(VideoState* state,
        AVChannelLayout* wantedChannelLayout,
        int wantedSampleRate,
        AudioParams* hardwareParams);
    void Pause(bool paused);
    void Close();
    [[nodiscard]] SDL_AudioDeviceID DeviceId() const noexcept;

    static int SynchronizeSamples(VideoState* state, int sampleCount);
    static int DecodeFrame(VideoState* state);

private:
    static void Callback(void* opaque, Uint8* stream, int length);
    static void UpdateSampleDisplay(VideoState* state, const int16_t* samples, int sampleCount);

    SDL_AudioDeviceID m_device{0};
};
