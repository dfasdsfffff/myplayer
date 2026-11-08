#include "soundtouch_wrap.h"
#ifdef MYPLAYER_USE_SOUNDTOUCH_DLL
#include <soundtouch/SoundTouchDLL.h>

void* soundtouch_create()
{
    HANDLE handle = soundtouch_createInstance();
    return handle;
}

int soundtouch_translate(void* handle, short* data, float tempo,
    int len, int bytes_per_sample, int n_channel, int n_sampleRate,
    short* output, int output_capacity_samples)
{
    HANDLE h = (HANDLE)handle;
    unsigned int nb = 0;
    int output_samples = 0;
    if (h == nullptr || !data || !output || n_channel <= 0 || output_capacity_samples <= 0)
        return 0;

    soundtouch_setSampleRate(h, n_sampleRate);
    soundtouch_setChannels(h, n_channel);
    // setTempo changes duration while keeping the original pitch.  Combining
    // setRate with an inverse setPitch adds an unnecessary resampling pass,
    // which is especially audible as artifacts at slow playback rates.
    soundtouch_setRate(h, 1.0f);
    soundtouch_setPitch(h, 1.0f);
    soundtouch_setTempo(h, tempo);

    soundtouch_putSamples_i16(h, data, len / n_channel);

    do {
        const unsigned int remaining_frames = static_cast<unsigned int>((output_capacity_samples - output_samples) / n_channel);
        if (!remaining_frames)
            break;
        nb = soundtouch_receiveSamples_i16(h, output + output_samples, remaining_frames);
        output_samples += static_cast<int>(nb) * n_channel;
    } while (nb != 0);

    return output_samples * bytes_per_sample;
}

void soundtouch_clear_samples(void* handle)
{
    if (handle)
        soundtouch_clear(static_cast<HANDLE>(handle));
}

void soundtouch_destroy(void* handle)
{
    HANDLE h = (HANDLE)handle;
    if (h == nullptr)
        return;
    soundtouch_clear_samples(handle);
    soundtouch_destroyInstance(h);
}
#else
#include <soundtouch/SoundTouch.h>

#include <algorithm>
#include <type_traits>
#include <vector>

namespace {
using Sample = soundtouch::SAMPLETYPE;

struct TempoProcessor {
    soundtouch::SoundTouch processor;
    std::vector<Sample> samples;
};

template<class T>
T ToSample(short sample)
{
    if constexpr (std::is_floating_point_v<T>)
        return static_cast<T>(sample) / 32768;
    else
        return sample;
}

template<class T>
short ToPcm16(T sample)
{
    if constexpr (std::is_floating_point_v<T>)
        return static_cast<short>(std::clamp(sample * 32768, T(-32768), T(32767)));
    else
        return sample;
}
}

void* soundtouch_create()
{
    return new TempoProcessor;
}

int soundtouch_translate(void* handle, short* data, float tempo,
    int len, int bytes_per_sample, int n_channel, int n_sampleRate,
    short* output, int output_capacity_samples)
{
    if (!handle || !data || !output || n_channel <= 0 || output_capacity_samples < n_channel || len < 0)
        return 0;

    auto& [processor, samples] = *static_cast<TempoProcessor*>(handle);
    processor.setSampleRate(n_sampleRate);
    processor.setChannels(n_channel);
    processor.setTempo(tempo);

    const int inputFrames = len / n_channel;
    samples.resize(inputFrames * n_channel);
    std::transform(data, data + samples.size(), samples.begin(), ToSample<Sample>);
    if (inputFrames)
        processor.putSamples(samples.data(), inputFrames);

    const int outputFrames = output_capacity_samples / n_channel;
    samples.resize(outputFrames * n_channel);
    const auto received = processor.receiveSamples(samples.data(), outputFrames) * n_channel;
    std::transform(samples.begin(), samples.begin() + received, output, ToPcm16<Sample>);
    return static_cast<int>(received) * bytes_per_sample;
}

void soundtouch_clear_samples(void* handle)
{
    if (handle)
        static_cast<TempoProcessor*>(handle)->processor.clear();
}

void soundtouch_destroy(void* handle)
{
    delete static_cast<TempoProcessor*>(handle);
}
#endif
