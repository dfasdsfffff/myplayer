#include "soundtouch_wrap.h"
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
    int put_n_sample = len / n_channel;
    unsigned int nb = 0;
    int output_samples = 0;
    if (h == NULL || !data || !output || n_channel <= 0 || output_capacity_samples <= 0)
        return 0;

    soundtouch_setSampleRate(h, n_sampleRate);
    soundtouch_setChannels(h, n_channel);
    // setTempo changes duration while keeping the original pitch.  Combining
    // setRate with an inverse setPitch adds an unnecessary resampling pass,
    // which is especially audible as artifacts at slow playback rates.
    soundtouch_setRate(h, 1.0f);
    soundtouch_setPitch(h, 1.0f);
    soundtouch_setTempo(h, tempo);

    soundtouch_putSamples_i16(h, data, put_n_sample);

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
    if (h == NULL)
        return;
    soundtouch_clear_samples(handle);
    soundtouch_destroyInstance(h);
}
