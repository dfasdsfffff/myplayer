#include "audio_output.h"

#include "audio_render_queue.h"
#include "media_sync.h"
#include "soundtouch_wrap.h"
#include "video_state.h"

#include <cstdlib>
#include <cstring>

AudioOutput::~AudioOutput()
{
    Close();
}

int AudioOutput::Open(VideoState* state,
    AVChannelLayout* wantedChannelLayout,
    int wantedSampleRate,
    AudioParams* hardwareParams)
{
    SDL_AudioSpec wantedSpec{}, spec{};
    const char* env;
    static const int nextChannelCounts[] = {0, 0, 1, 6, 2, 6, 4, 6};
    static const int nextSampleRates[] = {0, 44100, 48000, 96000, 192000};
    int nextSampleRateIndex = FF_ARRAY_ELEMS(nextSampleRates) - 1;
    int wantedChannelCount = wantedChannelLayout->nb_channels;

    env = SDL_getenv("SDL_AUDIO_CHANNELS");
    if (env) {
        wantedChannelCount = atoi(env);
        av_channel_layout_uninit(wantedChannelLayout);
        av_channel_layout_default(wantedChannelLayout, wantedChannelCount);
    }
    if (wantedChannelLayout->order != AV_CHANNEL_ORDER_NATIVE) {
        av_channel_layout_uninit(wantedChannelLayout);
        av_channel_layout_default(wantedChannelLayout, wantedChannelCount);
    }
    wantedChannelCount = wantedChannelLayout->nb_channels;
    wantedSpec.channels = wantedChannelCount;
    wantedSpec.freq = wantedSampleRate;
    if (wantedSpec.freq <= 0 || wantedSpec.channels <= 0) {
        av_log(nullptr, AV_LOG_ERROR, "Invalid sample rate or channel count!\n");
        return -1;
    }
    while (nextSampleRateIndex && nextSampleRates[nextSampleRateIndex] >= wantedSpec.freq)
        nextSampleRateIndex--;
    wantedSpec.format = AUDIO_S16SYS;
    wantedSpec.silence = 0;
    wantedSpec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wantedSpec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
    wantedSpec.callback = Callback;
    wantedSpec.userdata = state;
    while (!(m_device = SDL_OpenAudioDevice(nullptr, 0, &wantedSpec, &spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE))) {
        av_log(nullptr, AV_LOG_WARNING, "SDL_OpenAudio (%d channels, %d Hz): %s\n",
            wantedSpec.channels, wantedSpec.freq, SDL_GetError());
        wantedSpec.channels = nextChannelCounts[FFMIN(7, wantedSpec.channels)];
        if (!wantedSpec.channels) {
            wantedSpec.freq = nextSampleRates[nextSampleRateIndex--];
            wantedSpec.channels = wantedChannelCount;
            if (!wantedSpec.freq) {
                av_log(nullptr, AV_LOG_ERROR,
                    "No more combinations to try, audio open failed\n");
                return -1;
            }
        }
        av_channel_layout_default(wantedChannelLayout, wantedSpec.channels);
    }
    if (spec.format != AUDIO_S16SYS) {
        av_log(nullptr, AV_LOG_ERROR,
            "SDL advised audio format %d is not supported!\n", spec.format);
        Close();
        return -1;
    }
    if (spec.channels != wantedSpec.channels) {
        av_channel_layout_uninit(wantedChannelLayout);
        av_channel_layout_default(wantedChannelLayout, spec.channels);
        if (wantedChannelLayout->order != AV_CHANNEL_ORDER_NATIVE) {
            av_log(nullptr, AV_LOG_ERROR,
                "SDL advised channel count %d is not supported!\n", spec.channels);
            Close();
            return -1;
        }
    }

    hardwareParams->fmt = AV_SAMPLE_FMT_S16;
    hardwareParams->freq = spec.freq;
    if (av_channel_layout_copy(&hardwareParams->ch_layout, wantedChannelLayout) < 0) {
        Close();
        return -1;
    }
    hardwareParams->frame_size = av_samples_get_buffer_size(nullptr, hardwareParams->ch_layout.nb_channels, 1, hardwareParams->fmt, 1);
    hardwareParams->bytes_per_sec = av_samples_get_buffer_size(nullptr, hardwareParams->ch_layout.nb_channels, hardwareParams->freq, hardwareParams->fmt, 1);
    if (hardwareParams->bytes_per_sec <= 0 || hardwareParams->frame_size <= 0) {
        av_log(nullptr, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
        Close();
        return -1;
    }
    m_state = state;
    state->audio.renderQueue = std::make_unique<AudioRenderQueue>();
    if (!state->audio.renderQueue->start(state, *hardwareParams)) {
        state->audio.renderQueue.reset();
        Close();
        return -1;
    }
    return spec.size;
}

void AudioOutput::Pause(bool paused)
{
    if (m_device)
        SDL_PauseAudioDevice(m_device, paused ? 1 : 0);
}

void AudioOutput::Close()
{
    if (m_state && m_state->audio.renderQueue) {
        if (m_device)
            SDL_LockAudioDevice(m_device);
        m_state->audio.renderQueue->stop();
        m_state->audio.renderQueue.reset();
        if (m_device)
            SDL_UnlockAudioDevice(m_device);
    }
    if (m_device) {
        SDL_CloseAudioDevice(m_device);
        m_device = 0;
    }
    m_state = nullptr;
}

void AudioOutput::Flush()
{
    if (m_state && m_state->audio.renderQueue) {
        if (m_device)
            SDL_LockAudioDevice(m_device);
        m_state->audio.renderQueue->flush();
        if (m_device)
            SDL_UnlockAudioDevice(m_device);
    }
}

SDL_AudioDeviceID AudioOutput::DeviceId() const noexcept
{
    return m_device;
}

int AudioOutput::SynchronizeSamples(VideoState* state, int sampleCount)
{
    int wantedSampleCount = sampleCount;

    if (MediaSync::get_master_sync_type(state) != AV_SYNC_AUDIO_MASTER) {
        double diff, averageDiff;
        int minSampleCount, maxSampleCount;

        diff = state->clocks.audclk.get() - MediaSync::get_master_clock(state);

        if (!std::isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
            state->audio.audio_diff_cum = diff + state->audio.audio_diff_avg_coef * state->audio.audio_diff_cum;
            if (state->audio.audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
                state->audio.audio_diff_avg_count++;
            } else {
                averageDiff = state->audio.audio_diff_cum * (1.0 - state->audio.audio_diff_avg_coef);

                if (fabs(averageDiff) >= state->audio.audio_diff_threshold) {
                    wantedSampleCount = sampleCount + (int)(diff * state->audio.audio_src.freq);
                    minSampleCount = ((sampleCount * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    maxSampleCount = ((sampleCount * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    wantedSampleCount = av_clip(wantedSampleCount, minSampleCount, maxSampleCount);
                }
                av_log(nullptr, AV_LOG_TRACE, "diff=%f adiff=%f sample_diff=%d apts=%0.3f %f\n",
                    diff, averageDiff, wantedSampleCount - sampleCount,
                    state->audio.audio_clock, state->audio.audio_diff_threshold);
            }
        } else {
            state->audio.audio_diff_avg_count = 0;
            state->audio.audio_diff_cum = 0;
        }
    }

    return wantedSampleCount;
}

int AudioOutput::DecodeFrame(VideoState* state)
{
    int dataSize, resampledDataSize;
    av_unused double audioClock0;
    int wantedSampleCount;
    Frame* audioFrame;
    if (state->session.paused.load(std::memory_order_acquire))
        return -1;
reload:
    do {
#if defined(_WIN32)
        while (state->audio.sampq.nb_remaining() == 0 && !state->audio.audioq.isAborted()) {
            const auto waitStarted = av_gettime_relative();
            state->audio.sampq.wait_readable_for(100);
            if ((av_gettime_relative() - waitStarted) > 1000000LL * state->audio.audio_hw_buf_size / state->audio.audio_tgt.bytes_per_sec / 2)
                return -1;
        }

        if (state->audio.audioq.isAborted())
            return -1;
#endif
        if (!(audioFrame = state->audio.sampq.peek_readable()))
            return -1;
        state->audio.sampq.next();
    } while (audioFrame->serial != state->audio.audioq.serial());

    dataSize = av_samples_get_buffer_size(nullptr, audioFrame->frame->ch_layout.nb_channels,
        audioFrame->frame->nb_samples,
        (AVSampleFormat)audioFrame->frame->format, 1);

    wantedSampleCount = AudioOutput::SynchronizeSamples(state, audioFrame->frame->nb_samples);

    if (audioFrame->frame->format != state->audio.audio_src.fmt ||
        av_channel_layout_compare(&audioFrame->frame->ch_layout, &state->audio.audio_src.ch_layout) ||
        audioFrame->frame->sample_rate != state->audio.audio_src.freq ||
        (wantedSampleCount != audioFrame->frame->nb_samples && !state->audio.swr_ctx)) {
        swr_free(&state->audio.swr_ctx);
        swr_alloc_set_opts2(&state->audio.swr_ctx,
            &state->audio.audio_tgt.ch_layout, state->audio.audio_tgt.fmt, state->audio.audio_tgt.freq,
            &audioFrame->frame->ch_layout, (AVSampleFormat)audioFrame->frame->format, audioFrame->frame->sample_rate,
            0, nullptr);
        if (!state->audio.swr_ctx || swr_init(state->audio.swr_ctx) < 0) {
            av_log(nullptr, AV_LOG_ERROR,
                "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                audioFrame->frame->sample_rate, av_get_sample_fmt_name((AVSampleFormat)audioFrame->frame->format), audioFrame->frame->ch_layout.nb_channels,
                state->audio.audio_tgt.freq, av_get_sample_fmt_name(state->audio.audio_tgt.fmt), state->audio.audio_tgt.ch_layout.nb_channels);
            swr_free(&state->audio.swr_ctx);
            return -1;
        }
        if (av_channel_layout_copy(&state->audio.audio_src.ch_layout, &audioFrame->frame->ch_layout) < 0)
            return -1;
        state->audio.audio_src.freq = audioFrame->frame->sample_rate;
        state->audio.audio_src.fmt = (AVSampleFormat)audioFrame->frame->format;
    }

    if (state->audio.swr_ctx) {
        const uint8_t** in = (const uint8_t**)audioFrame->frame->extended_data;
        uint8_t** out = &state->audio.audio_buf1;
        int outCount = (int64_t)wantedSampleCount * state->audio.audio_tgt.freq / audioFrame->frame->sample_rate + 256;
        int outSize = av_samples_get_buffer_size(nullptr, state->audio.audio_tgt.ch_layout.nb_channels, outCount, state->audio.audio_tgt.fmt, 0);
        int len2;
        if (outSize < 0) {
            av_log(nullptr, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
            return -1;
        }
        if (wantedSampleCount != audioFrame->frame->nb_samples) {
            if (swr_set_compensation(state->audio.swr_ctx,
                (wantedSampleCount - audioFrame->frame->nb_samples) * state->audio.audio_tgt.freq / audioFrame->frame->sample_rate,
                wantedSampleCount * state->audio.audio_tgt.freq / audioFrame->frame->sample_rate) < 0) {
                av_log(nullptr, AV_LOG_ERROR, "swr_set_compensation() failed\n");
                return -1;
            }
        }
        av_fast_malloc(&state->audio.audio_buf1, &state->audio.audio_buf1_size, outSize);
        if (!state->audio.audio_buf1)
            return AVERROR(ENOMEM);
        len2 = swr_convert(state->audio.swr_ctx, out, outCount, in, audioFrame->frame->nb_samples);
        if (len2 < 0) {
            av_log(nullptr, AV_LOG_ERROR, "swr_convert() failed\n");
            return -1;
        }
        if (len2 == outCount) {
            av_log(nullptr, AV_LOG_WARNING, "audio buffer is probably too small\n");
            if (swr_init(state->audio.swr_ctx) < 0)
                swr_free(&state->audio.swr_ctx);
        }
        state->audio.audio_buf = state->audio.audio_buf1;
        resampledDataSize = len2 * state->audio.audio_tgt.ch_layout.nb_channels * av_get_bytes_per_sample(state->audio.audio_tgt.fmt);
        int bytesPerSample = av_get_bytes_per_sample(state->audio.audio_tgt.fmt);
        const auto playbackRate = state->audio.play_rate.load(std::memory_order_acquire);
        if (state->audio.soundTouchHandle && playbackRate != 1.0 && !state->session.abort_request.load(std::memory_order_acquire)) {
            av_fast_malloc(&state->audio.audio_new_buf, &state->audio.audio_new_buf_size, outSize * 4);
            if (!state->audio.audio_new_buf)
                return AVERROR(ENOMEM);
            int translatedLength = soundtouch_translate(state->audio.soundTouchHandle,
                reinterpret_cast<short*>(state->audio.audio_buf1),
                static_cast<float>(playbackRate),
                static_cast<float>(1.0 / playbackRate),
                resampledDataSize / 2,
                bytesPerSample,
                state->audio.audio_tgt.ch_layout.nb_channels,
                audioFrame->frame->sample_rate,
                state->audio.audio_new_buf,
                static_cast<int>(state->audio.audio_new_buf_size / sizeof(short)));
            if (translatedLength > 0) {
                state->audio.audio_buf = (uint8_t*)state->audio.audio_new_buf;
                resampledDataSize = translatedLength;
            } else {
                goto reload;
            }
        }
    } else {
        state->audio.audio_buf = audioFrame->frame->data[0];
        resampledDataSize = dataSize;
    }

    audioClock0 = state->audio.audio_clock;
    if (!isnan(audioFrame->pts))
        state->audio.audio_clock = audioFrame->pts + (double)audioFrame->frame->nb_samples / audioFrame->frame->sample_rate;
    else
        state->audio.audio_clock = NAN;
    state->audio.audio_clock_serial = audioFrame->serial;
    return resampledDataSize;
}

void AudioOutput::Callback(void* opaque, Uint8* stream, int length)
{
    auto* state = static_cast<VideoState*>(opaque);
    const auto callbackTime = av_gettime_relative();
    auto* renderQueue = state->audio.renderQueue.get();
    if (!renderQueue) {
        memset(stream, 0, length);
        return;
    }

    renderQueue->read(stream, static_cast<size_t>(length));
    const auto audioVolume = state->audio.audio_volume.load(std::memory_order_relaxed);
    if (audioVolume != SDL_MIX_MAXVOLUME) {
        auto* samples = reinterpret_cast<int16_t*>(stream);
        for (int index = 0; index < length / static_cast<int>(sizeof(int16_t)); ++index)
            samples[index] = static_cast<int16_t>(static_cast<int>(samples[index]) * audioVolume / SDL_MIX_MAXVOLUME);
    }
    const double audioClock = renderQueue->producerClock();
    if (!std::isnan(audioClock)) {
        state->clocks.audclk.set_at(
            audioClock - static_cast<double>(2 * state->audio.audio_hw_buf_size + renderQueue->pendingBytes()) / state->audio.audio_tgt.bytes_per_sec,
            renderQueue->producerClockSerial(),
            callbackTime / 1000000.0);
        state->clocks.extclk.sync_to_slave(state->clocks.audclk);
    }
}

void AudioOutput::UpdateSampleDisplay(VideoState* state, const int16_t* samples, int sampleCount)
{
    int size = sampleCount;
    while (size > 0) {
        int length = SAMPLE_ARRAY_SIZE - state->audio.sample_array_index;
        if (length > size)
            length = size;
        memcpy(state->audio.sample_array + state->audio.sample_array_index, samples, length * sizeof(int16_t));
        samples += length;
        state->audio.sample_array_index += length;
        if (state->audio.sample_array_index >= SAMPLE_ARRAY_SIZE)
            state->audio.sample_array_index = 0;
        size -= length;
    }
}
