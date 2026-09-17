#include "stream_reader.h"

#include "av_constants.h"
#include "media_sync.h"
#include "network_input.h"
#include "video_state.h"

#include <cstring>
#include <stdexcept>
#include <thread>

namespace {

void PrintError(const char* message, int error)
{
    char buffer[256];
    av_strerror(error, buffer, sizeof(buffer));
    av_log(nullptr, AV_LOG_ERROR, "%s: %s\n", message, buffer);
}

int DecodeInterruptCallback(void* context)
{
    auto* state = static_cast<VideoState*>(context);
    return state->session.abort_request || InterruptNetworkIo(&state->session.io);
}

void TogglePause(VideoState* state)
{
    if (state->session.paused) {
        state->video.frame_timer += av_gettime_relative() / 1000000.0 - state->clocks.vidclk.last_updated;
        if (state->session.read_pause_return != AVERROR(ENOSYS))
            state->clocks.vidclk.paused = 0;
        state->clocks.vidclk.set(state->clocks.vidclk.get(),
            state->clocks.vidclk.serial.load(std::memory_order_acquire));
    }
    state->clocks.extclk.set(state->clocks.extclk.get(),
        state->clocks.extclk.serial.load(std::memory_order_acquire));
    const int paused = !state->session.paused.load(std::memory_order_acquire);
    state->clocks.audclk.paused = paused;
    state->clocks.vidclk.paused = paused;
    state->clocks.extclk.paused = paused;
    state->session.paused.store(paused, std::memory_order_release);
}

void StepToNextFrame(VideoState* state)
{
    if (state->session.paused)
        TogglePause(state);
    state->video.step = 1;
}

void WaitForReadSignal(VideoState* state)
{
    SDL_LockMutex(state->session.read_wait_mutex);
    SDL_CondWaitTimeout(state->session.continue_read_thread, state->session.read_wait_mutex, 10);
    SDL_UnlockMutex(state->session.read_wait_mutex);
}

} // namespace

StreamReader::StreamReader(StreamReaderCallbacks callbacks)
    : m_callbacks(std::move(callbacks))
{
    if (!m_callbacks.openComponent ||
        !m_callbacks.publishStatus ||
        !m_callbacks.publishDurationSeconds ||
        !m_callbacks.publishMediaInfo ||
        !m_callbacks.stopRefreshLoop ||
        !m_callbacks.handleEndOfMedia) {
        throw std::invalid_argument("StreamReader requires all callbacks");
    }
}

bool StreamReader::HasEnoughPackets(AVStream* stream, int streamId, PacketQueue* queue)
{
    return streamId < 0 ||
        queue->abort_request ||
        (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
        queue->nb_packets > MIN_FRAMES && (!queue->duration || av_q2d(stream->time_base) * queue->duration > 1.0);
}

bool StreamReader::IsRealtime(const AVFormatContext* context)
{
    if (!strcmp(context->iformat->name, "rtp") ||
        !strcmp(context->iformat->name, "rtsp") ||
        !strcmp(context->iformat->name, "sdp")) {
        return true;
    }

    if (context->pb &&
        (!strncmp(context->url, "rtp:", 4) ||
            !strncmp(context->url, "udp:", 4))) {
        return true;
    }

    return false;
}

void StreamReader::Run(VideoState* state)
{
    AVFormatContext* inputContext = nullptr;
    int err = 0;
    int ret = 0;
    int streamIndexes[AVMEDIA_TYPE_NB];
    AVPacket* packet = nullptr;
    int64_t streamStartTime = 0;
    int packetInPlayRange = 0;
    AVDictionary** opts = nullptr;
    AvDictionary inputOptions;
    int64_t packetTimestamp = 0;
    const char* wantedStreamSpec[AVMEDIA_TYPE_NB] = {0};

    SDL_mutex* waitMutex = SDL_CreateMutex();
    if (!waitMutex) {
        av_log(nullptr, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    state->session.read_wait_mutex = waitMutex;
    memset(streamIndexes, -1, sizeof(streamIndexes));
    state->session.eof = 0;

    packet = av_packet_alloc();
    if (!packet) {
        av_log(nullptr, AV_LOG_FATAL, "Could not allocate packet.\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    inputContext = avformat_alloc_context();
    if (!inputContext) {
        av_log(nullptr, AV_LOG_FATAL, "Could not allocate context.\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    inputContext->interrupt_callback.callback = DecodeInterruptCallback;
    inputContext->interrupt_callback.opaque = state;

    inputOptions = BuildInputOptions(state->session.source);
    state->session.io.begin(IoOperation::Opening, state->session.source.network.connectTimeout);
    err = avformat_open_input(&inputContext, state->session.filename, nullptr, inputOptions.put());
    state->session.io.end();
    if (err < 0) {
        PrintError(state->session.filename, err);
        state->session.readResult.store(err, std::memory_order_release);
        state->session.readError.store(
            MapAvError(err, IsRealtimeSource(ClassifyMediaSource(state->session.source.location))),
            std::memory_order_release);
        ret = err;
        goto fail;
    }

    state->session.ic = inputContext;

    state->session.io.begin(IoOperation::Probing,
        std::chrono::duration_cast<std::chrono::milliseconds>(state->session.source.network.analyzeDuration));
    err = avformat_find_stream_info(inputContext, opts);
    state->session.io.end();

    if (err < 0) {
        av_log(nullptr, AV_LOG_WARNING,
            "%s: could not find codec parameters\n", state->session.filename);
        state->session.readResult.store(err, std::memory_order_release);
        state->session.readError.store(
            MapAvError(err, IsRealtimeSource(ClassifyMediaSource(state->session.source.location))),
            std::memory_order_release);
        ret = err;
        goto fail;
    }

    if (inputContext->pb)
        inputContext->pb->eof_reached = 0;

    state->video.max_frame_duration = (inputContext->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;
    state->session.realtime = IsRealtime(inputContext);
    state->session.mediaInfo = BuildMediaInfo(state->session.source, inputContext);
    state->session.unlimitedBuffer = UseUnlimitedBuffer(state->session.source, state->session.realtime != 0);
    m_callbacks.publishMediaInfo(state->session.mediaInfo);
    m_callbacks.publishDurationSeconds(
        state->session.mediaInfo.duration ? static_cast<int>(state->session.mediaInfo.duration->count() / 1000) : 0);

    for (int i = 0; i < inputContext->nb_streams; i++) {
        AVStream* stream = inputContext->streams[i];
        enum AVMediaType type = stream->codecpar->codec_type;
        stream->discard = AVDISCARD_ALL;
        if (type >= 0 && wantedStreamSpec[type] && streamIndexes[type] == -1)
            if (avformat_match_stream_specifier(inputContext, stream, wantedStreamSpec[type]) > 0)
                streamIndexes[type] = i;
    }
    for (int i = 0; i < AVMEDIA_TYPE_NB; i++) {
        if (wantedStreamSpec[(AVMediaType)i] && streamIndexes[(AVMediaType)i] == -1) {
            av_log(nullptr, AV_LOG_ERROR, "Stream specifier %s does not match any %s stream\n",
                wantedStreamSpec[(AVMediaType)i], av_get_media_type_string((AVMediaType)i));
            streamIndexes[(AVMediaType)i] = INT_MAX;
        }
    }

    streamIndexes[AVMEDIA_TYPE_VIDEO] =
        av_find_best_stream(inputContext, AVMEDIA_TYPE_VIDEO,
            streamIndexes[AVMEDIA_TYPE_VIDEO], -1, nullptr, 0);

    streamIndexes[AVMEDIA_TYPE_AUDIO] =
        av_find_best_stream(inputContext, AVMEDIA_TYPE_AUDIO,
            streamIndexes[AVMEDIA_TYPE_AUDIO],
            streamIndexes[AVMEDIA_TYPE_VIDEO],
            nullptr, 0);

    streamIndexes[AVMEDIA_TYPE_SUBTITLE] =
        av_find_best_stream(inputContext, AVMEDIA_TYPE_SUBTITLE,
            streamIndexes[AVMEDIA_TYPE_SUBTITLE],
            (streamIndexes[AVMEDIA_TYPE_AUDIO] >= 0 ?
                streamIndexes[AVMEDIA_TYPE_AUDIO] :
                streamIndexes[AVMEDIA_TYPE_VIDEO]),
            nullptr, 0);

    if (streamIndexes[AVMEDIA_TYPE_AUDIO] >= 0)
        m_callbacks.openComponent(state, streamIndexes[AVMEDIA_TYPE_AUDIO]);

    ret = -1;
    if (streamIndexes[AVMEDIA_TYPE_VIDEO] >= 0)
        ret = m_callbacks.openComponent(state, streamIndexes[AVMEDIA_TYPE_VIDEO]);

    if (streamIndexes[AVMEDIA_TYPE_SUBTITLE] >= 0)
        m_callbacks.openComponent(state, streamIndexes[AVMEDIA_TYPE_SUBTITLE]);

    if (state->video.video_stream < 0 && state->audio.audio_stream < 0) {
        av_log(nullptr, AV_LOG_FATAL, "Failed to open file '%s' or configure filtergraph\n",
            state->session.filename);
        ret = -1;
        goto fail;
    }
    m_callbacks.publishStatus(PlaybackStatus{PlaybackState::Playing, PlaybackError::None, 0,
        state->session.source.network.maxReconnectAttempts, {},
        RedactMediaLocation(state->session.source.location)});

    for (;;) {
        if (state->session.abort_request.load(std::memory_order_acquire))
            break;
        const auto paused = state->session.paused.load(std::memory_order_acquire);
        if (paused != state->session.last_paused) {
            state->session.last_paused = paused;
            if (paused)
                state->session.read_pause_return = av_read_pause(inputContext);
            else
                av_read_play(inputContext);
        }

        int64_t seekTarget = 0;
        int64_t seekRelative = 0;
        int seekFlags = 0;
        bool hasSeekRequest = false;
        {
            std::lock_guard<std::mutex> seekLock(state->session.seek_mutex);
            if (state->session.seek_req) {
                seekTarget = state->session.seek_pos;
                seekRelative = state->session.seek_rel;
                seekFlags = state->session.seek_flags;
                state->session.seek_req = false;
                hasSeekRequest = true;
            }
        }
        if (hasSeekRequest) {
            const int64_t seekMin = seekRelative > 0 ? seekTarget - seekRelative + 2 : INT64_MIN;
            const int64_t seekMax = seekRelative < 0 ? seekTarget - seekRelative - 2 : INT64_MAX;
            ret = avformat_seek_file(state->session.ic, -1, seekMin, seekTarget, seekMax, seekFlags);
            if (ret < 0) {
                av_log(nullptr, AV_LOG_ERROR,
                    "%s: error while seeking\n", state->session.ic->url);
            } else {
                if (state->audio.audio_stream >= 0)
                    packet_queue_flush(&state->audio.audioq);
                if (state->subtitle.subtitle_stream >= 0)
                    packet_queue_flush(&state->subtitle.subtitleq);
                if (state->video.video_stream >= 0)
                    packet_queue_flush(&state->video.videoq);
                if (seekFlags & AVSEEK_FLAG_BYTE)
                    state->clocks.extclk.set(NAN, 0);
                else
                    state->clocks.extclk.set(seekTarget / static_cast<double>(AV_TIME_BASE), 0);
            }
            state->session.queue_attachments_req = 1;
            state->session.eof = 0;
            if (paused)
                StepToNextFrame(state);
        }
        if (state->session.queue_attachments_req) {
            if (state->video.video_st && state->video.video_st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                if ((ret = av_packet_ref(packet, &state->video.video_st->attached_pic)) < 0)
                    goto fail;
                packet_queue_put(&state->video.videoq, packet);
                packet_queue_put_nullpacket(&state->video.videoq, packet, state->video.video_stream);
            }
            state->session.queue_attachments_req = 0;
        }

        if (!state->session.unlimitedBuffer &&
            (state->audio.audioq.size.load(std::memory_order_relaxed)
                + state->video.videoq.size.load(std::memory_order_relaxed)
                + state->subtitle.subtitleq.size.load(std::memory_order_relaxed) > MAX_QUEUE_SIZE
                || (HasEnoughPackets(state->audio.audio_st, state->audio.audio_stream, &state->audio.audioq) &&
                    HasEnoughPackets(state->video.video_st, state->video.video_stream, &state->video.videoq) &&
                    HasEnoughPackets(state->subtitle.subtitle_st, state->subtitle.subtitle_stream, &state->subtitle.subtitleq)))) {
            WaitForReadSignal(state);
            continue;
        }
        if (!paused &&
            (!state->audio.audio_st || (state->audio.aud_decoder.finished == state->audio.audioq.serial.load(std::memory_order_acquire) && state->audio.sampq.nb_remaining() == 0)) &&
            (!state->video.video_st || (state->video.vid_decoder.finished == state->video.videoq.serial.load(std::memory_order_acquire) && state->video.pictq.nb_remaining() == 0))) {
            m_callbacks.handleEndOfMedia();
            continue;
        }

        state->session.io.begin(IoOperation::Reading, state->session.source.network.readTimeout);
        ret = av_read_frame(inputContext, packet);
        state->session.io.end();
        if (ret < 0) {
            state->session.readResult.store(ret, std::memory_order_release);
            state->session.readError.store(MapAvError(ret, state->session.realtime != 0), std::memory_order_release);
            if ((ret == AVERROR_EOF || avio_feof(inputContext->pb)) && !state->session.eof) {
                if (state->video.video_stream >= 0)
                    packet_queue_put_nullpacket(&state->video.videoq, packet, state->video.video_stream);
                if (state->audio.audio_stream >= 0)
                    packet_queue_put_nullpacket(&state->audio.audioq, packet, state->audio.audio_stream);
                if (state->subtitle.subtitle_stream >= 0)
                    packet_queue_put_nullpacket(&state->subtitle.subtitleq, packet, state->subtitle.subtitle_stream);
                state->session.eof = 1;
            }
            if (inputContext->pb && inputContext->pb->error)
                break;
            WaitForReadSignal(state);
            continue;
        } else {
            state->session.eof = 0;
        }

        streamStartTime = inputContext->streams[packet->stream_index]->start_time;
        packetTimestamp = packet->pts == AV_NOPTS_VALUE ? packet->dts : packet->pts;
        packetInPlayRange = AV_NOPTS_VALUE == AV_NOPTS_VALUE ||
            (packetTimestamp - (streamStartTime != AV_NOPTS_VALUE ? streamStartTime : 0)) *
            av_q2d(inputContext->streams[packet->stream_index]->time_base) -
            (double)(0 != AV_NOPTS_VALUE ? 0 : 0) / 1000000
            <= ((double)AV_NOPTS_VALUE / 1000000);
        if (packet->stream_index == state->audio.audio_stream && packetInPlayRange) {
            packet_queue_put(&state->audio.audioq, packet);
        } else if (packet->stream_index == state->video.video_stream && packetInPlayRange
            && !(state->video.video_st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            packet_queue_put(&state->video.videoq, packet);
        } else if (packet->stream_index == state->subtitle.subtitle_stream && packetInPlayRange) {
            packet_queue_put(&state->subtitle.subtitleq, packet);
        } else {
            av_packet_unref(packet);
        }
    }

    ret = 0;
fail:
    if (packet)
        av_packet_free(&packet);
    if (inputContext && !state->session.ic)
        avformat_close_input(&inputContext);
    if (ret != 0) {
        const auto error = state->session.readError.load(std::memory_order_acquire);
        m_callbacks.publishStatus(PlaybackStatus{PlaybackState::Failed, error, 0,
            state->session.source.network.maxReconnectAttempts, {},
            RedactMediaLocation(state->session.source.location)});
        m_callbacks.stopRefreshLoop();
    }
    if (state->session.read_wait_mutex) {
        SDL_DestroyMutex(state->session.read_wait_mutex);
        state->session.read_wait_mutex = nullptr;
    }
}
