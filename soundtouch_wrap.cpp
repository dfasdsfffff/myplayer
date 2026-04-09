#include "soundtouch_wrap.h"
#include "SoundTouch.h"

using namespace std;
using namespace soundtouch;

void* soundtouch_create()
{
    SoundTouch* handle_ptr = new SoundTouch();
    const char* version = handle_ptr->getVersionString();
    return handle_ptr;
}

int soundtouch_translate(void* handle, short* data, float speed, float pitch,
    int len, int bytes_per_sample, int n_channel, int n_sampleRate)
{
    SoundTouch* handle_ptr = (SoundTouch*)handle;
    int put_n_sample = len / n_channel;
    int nb = 0;
    int pcm_data_size = 0;
    if (handle_ptr == NULL)
        return 0;

    handle_ptr->setPitch(pitch);
    handle_ptr->setRate(speed);

    handle_ptr->setSampleRate(n_sampleRate);
    handle_ptr->setChannels(n_channel);

    handle_ptr->putSamples((SAMPLETYPE*)data, put_n_sample);

    do {
        nb = handle_ptr->receiveSamples((SAMPLETYPE*)data, n_sampleRate / n_channel);
        pcm_data_size += nb * n_channel * bytes_per_sample;
    } while (nb != 0);

    return pcm_data_size;
}

void soundtouch_destroy(void* handle)
{
    SoundTouch* handle_ptr = (SoundTouch*)handle;
    if (handle_ptr == NULL)
        return;
    handle_ptr->clear();
    delete handle_ptr;
    handle_ptr = NULL;
}
