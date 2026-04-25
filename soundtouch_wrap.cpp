#include "soundtouch_wrap.h"
#include "SoundTouchDLL.h"

void* soundtouch_create()
{
    HANDLE handle = soundtouch_createInstance();
    return handle;
}

int soundtouch_translate(void* handle, short* data, float speed, float pitch,
    int len, int bytes_per_sample, int n_channel, int n_sampleRate)
{
    HANDLE h = (HANDLE)handle;
    int put_n_sample = len / n_channel;
    unsigned int nb = 0;
    int pcm_data_size = 0;
    if (h == NULL)
        return 0;

    soundtouch_setPitch(h, pitch);
    soundtouch_setRate(h, speed);

    soundtouch_setSampleRate(h, n_sampleRate);
    soundtouch_setChannels(h, n_channel);

    soundtouch_putSamples_i16(h, data, put_n_sample);

    do {
        nb = soundtouch_receiveSamples_i16(h, data, n_sampleRate / n_channel);
        pcm_data_size += nb * n_channel * bytes_per_sample;
    } while (nb != 0);

    return pcm_data_size;
}

void soundtouch_destroy(void* handle)
{
    HANDLE h = (HANDLE)handle;
    if (h == NULL)
        return;
    soundtouch_clear(h);
    soundtouch_destroyInstance(h);
}
