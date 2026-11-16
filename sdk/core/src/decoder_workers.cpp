#include "decoder_workers.h"

#include "subtitle_dispatcher.h"

#include "filter_configurator.h"
#include "media_sync.h"

#include <cmath>
#include <mutex>
#include <string>

namespace {

int framedrop = 1;

static inline int cmp_audio_fmts(enum AVSampleFormat fmt1, int64_t channel_count1,
    enum AVSampleFormat fmt2, int64_t channel_count2)
{
    /* 如果通道计数== 1，平面格式和非平面格式是相同的 */
    if (channel_count1 == 1 && channel_count2 == 1)
        return av_get_packed_sample_fmt(fmt1) != av_get_packed_sample_fmt(fmt2);
    else
        return channel_count1 != channel_count2 || fmt1 != fmt2;
}

void ReportDecoderFailure(VideoState* state, PacketQueue& queue, int error)
{
    // Queue aborts and session shutdown are expected cancellation paths. They
    // must never become a DecoderFailure status.
    if (!state || error >= 0 || state->session.abort_request.load(std::memory_order_acquire) || queue.isAborted())
        return;

    int expected = 0;
    if (state->session.decoderError.compare_exchange_strong(
            expected, error, std::memory_order_release, std::memory_order_relaxed) &&
        state->session.continue_read_thread) {
        SDL_CondSignal(state->session.continue_read_thread);
    }
}

} // namespace

int DecoderWorkers::QueuePicture(VideoState* state, AVFrame* sourceFrame, double pts, double duration, int64_t pos, int serial)
{
    Frame* vp;

    if (!(vp = state->video.pictq.peek_writable()))
        return -1;

    vp->sar = sourceFrame->sample_aspect_ratio;
    vp->width = sourceFrame->width;
    vp->height = sourceFrame->height;
    vp->format = sourceFrame->format;
    vp->pts = pts;
    vp->duration = duration;
    vp->pos = pos;
    vp->serial = serial;

    av_frame_move_ref(vp->frame, sourceFrame);
    state->video.pictq.push();
    return 0;
}

namespace {

struct StepScanCollector {
    AVFrame* candidate{nullptr};
    double pts{NAN};
    double duration{0};
    int64_t pos{-1};
    int serial{-1};
    bool valid{false};
    bool finalized{false};
    int scanSerial{-1};
    int64_t lastTimestampUs{AV_NOPTS_VALUE};
};

int64_t FrameTimestampUs(VideoState* state, double pts)
{
    if (!state || !std::isfinite(pts))
        return AV_NOPTS_VALUE;
    return static_cast<int64_t>(std::llround(pts * AV_TIME_BASE));
}

void PublishStepError(VideoState* state, int serial, int error)
{
    std::lock_guard lock(state->session.step.mutex);
    if (state->session.step.active && state->session.step.serial.load(std::memory_order_acquire) == serial)
        state->session.step.error.store(error, std::memory_order_release);
}

int FinishStepScan(VideoState* state, StepScanCollector& collector, AVFrame* successor = nullptr,
    double successorPts = NAN, double successorDuration = 0, int64_t successorPos = -1, int successorSerial = -1)
{
    if (collector.finalized || collector.scanSerial != state->session.step.serial.load(std::memory_order_acquire))
        return 0;
    collector.finalized = true;
    if (!collector.valid) {
        PublishStepError(state, collector.scanSerial, AVERROR(EINVAL));
        return 0;
    }
    int ret = DecoderWorkers::QueuePicture(state, collector.candidate, collector.pts, collector.duration,
        collector.pos, collector.serial);
    if (ret >= 0 && successor)
        ret = DecoderWorkers::QueuePicture(state, successor, successorPts, successorDuration,
            successorPos, successorSerial);
    if (ret < 0)
        return ret;
    std::lock_guard lock(state->session.step.mutex);
    if (collector.scanSerial != state->session.step.serial.load(std::memory_order_acquire) ||
        !state->session.step.active.load(std::memory_order_acquire))
        return 0;
    state->session.step.scan.store(false, std::memory_order_release);
    state->session.step.ready.store(true, std::memory_order_release);
    return 0;
}

int CollectStepFrame(VideoState* state, StepScanCollector& collector, AVFrame* frame,
    double pts, double duration, int64_t pos, int serial)
{
    if (!state->session.step.scan.load(std::memory_order_acquire))
        return DecoderWorkers::QueuePicture(state, frame, pts, duration, pos, serial);
    if (serial != state->session.step.serial.load(std::memory_order_acquire)) {
        av_frame_unref(frame);
        return 0;
    }
    if (collector.scanSerial != serial) {
        if (collector.candidate) av_frame_unref(collector.candidate);
        collector.scanSerial = serial;
        collector.valid = false;
        collector.finalized = false;
        collector.lastTimestampUs = AV_NOPTS_VALUE;
    }
    int64_t target;
    int direction;
    {
        std::lock_guard<std::mutex> lock(state->session.step.mutex);
        target = state->session.step.targetPtsUs;
        direction = state->session.step.direction;
    }
    const auto timestamp = FrameTimestampUs(state, pts);
    if (timestamp == AV_NOPTS_VALUE ||
        (collector.lastTimestampUs != AV_NOPTS_VALUE && timestamp <= collector.lastTimestampUs)) {
        PublishStepError(state, serial, AVERROR(EINVAL));
        av_frame_unref(frame);
        return 0;
    }
    collector.lastTimestampUs = timestamp;
    if (direction < 0 && target != AV_NOPTS_VALUE && timestamp >= target && collector.valid)
        return FinishStepScan(state, collector, frame, pts, duration, pos, serial);
    if (!collector.candidate)
        collector.candidate = av_frame_alloc();
    if (!collector.candidate)
        return AVERROR(ENOMEM);
    av_frame_unref(collector.candidate);
    av_frame_move_ref(collector.candidate, frame);
    collector.pts = pts;
    collector.duration = duration;
    collector.pos = pos;
    collector.serial = serial;
    collector.valid = true;
    if (target == AV_NOPTS_VALUE || (direction > 0 ? timestamp > target : timestamp >= target))
        return FinishStepScan(state, collector);
    return 0;
}

} // namespace

int DecoderWorkers::GetVideoFrame(VideoState* state, AVFrame* frame)
{
    int got_picture;

    if ((got_picture = state->video.vid_decoder.decode_frame(frame, NULL)) < 0)
        return got_picture;

    if (got_picture) {
        double dpts = NAN;

        if (frame->pts != AV_NOPTS_VALUE)
            dpts = av_q2d(state->video.video_st->time_base) * frame->pts;
        frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(state->session.ic, state->video.video_st, frame);

        if (!state->session.paused.load(std::memory_order_acquire) &&
            !state->session.step.active.load(std::memory_order_acquire) &&
            !state->video.step.load(std::memory_order_acquire) &&
            !state->session.step.scan.load(std::memory_order_acquire) &&
            (framedrop > 0
            || (framedrop && MediaSync::get_master_sync_type(state) != AV_SYNC_VIDEO_MASTER)
            )) {
            if (frame->pts != AV_NOPTS_VALUE) {
                double diff = dpts - MediaSync::get_master_clock(state);
                if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
                    diff - state->video.frame_last_filter_delay < 0 &&
					state->video.vid_decoder.pkt_serial == state->clocks.vidclk.serial() &&
                    state->video.videoq.snapshot().packets) {
                    state->video.frame_drops_early++;
                    av_frame_unref(frame);
                    got_picture = 0;
                }
            }
        }
    }

    return got_picture;
}

int DecoderWorkers::Audio(void* opaque)
{
    VideoState* state = (VideoState*)opaque;
    AVFrame* frame = av_frame_alloc();
    Frame* af;
    int reconfigure;
    int got_frame = 0;
    AVRational tb;
    int ret = 0;
    int last_serial = -1;

    if (!frame) {
        ReportDecoderFailure(state, state->audio.audioq, AVERROR(ENOMEM));
        return AVERROR(ENOMEM);
    }

    do {
        if ((got_frame = state->audio.aud_decoder.decode_frame(frame, NULL)) < 0) {
            ret = got_frame;
            goto the_end;
        }

        if (got_frame) {
            tb = AVRational{ 1, frame->sample_rate };
            tb = AVRational{ 1, frame->sample_rate };
#if CONFIG_AVFILTER
            reconfigure =
                cmp_audio_fmts(state->audio.audio_filter_src.fmt, state->audio.audio_filter_src.ch_layout.nb_channels,
                    static_cast<AVSampleFormat>(frame->format), frame->ch_layout.nb_channels) ||
                av_channel_layout_compare(&state->audio.audio_filter_src.ch_layout, &frame->ch_layout) ||
                state->audio.audio_filter_src.freq != frame->sample_rate ||
                state->audio.aud_decoder.pkt_serial != last_serial;

            if (reconfigure)
            {
                char buf1[1024], buf2[1024];
                av_channel_layout_describe(&state->audio.audio_filter_src.ch_layout, buf1, sizeof(buf1));
                av_channel_layout_describe(&frame->ch_layout, buf2, sizeof(buf2));
                av_log(NULL, AV_LOG_DEBUG,
                    "Audio frame changed from rate:%d ch:%d fmt:%s layout:%s serial:%d to rate:%d ch:%d fmt:%s layout:%s serial:%d\n",
                    state->audio.audio_filter_src.freq, state->audio.audio_filter_src.ch_layout.nb_channels, av_get_sample_fmt_name(state->audio.audio_filter_src.fmt), buf1, last_serial,
                    frame->sample_rate, frame->ch_layout.nb_channels, av_get_sample_fmt_name(static_cast<AVSampleFormat>(frame->format)), buf2, state->audio.aud_decoder.pkt_serial);

                state->audio.audio_filter_src.fmt = static_cast<AVSampleFormat>(frame->format);
                ret = av_channel_layout_copy(&state->audio.audio_filter_src.ch_layout, &frame->ch_layout);
                if (ret < 0)
                {
                    goto the_end;
                }
                state->audio.audio_filter_src.freq = frame->sample_rate;
                last_serial = state->audio.aud_decoder.pkt_serial;
                const auto currentSpeed = state->audio.play_rate.load(std::memory_order_acquire);
                auto afilters = "atempo=" + std::to_string(currentSpeed);
                if ((ret = ConfigureAudioFilters(state, afilters.c_str(), true)) < 0)
                {
                    goto the_end;
                }
            }
            if ((ret = av_buffersrc_add_frame(state->filters.in_audio_filter, frame)) < 0)
                goto the_end;

            while ((ret = av_buffersink_get_frame_flags(state->filters.out_audio_filter, frame, 0)) >= 0)
            {
                FrameData* fd = frame->opaque_ref ? (FrameData*)frame->opaque_ref->data : NULL;
                tb = av_buffersink_get_time_base(state->filters.out_audio_filter);
                if (!(af = state->audio.sampq.peek_writable()))
                    goto the_end;

                af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
                af->pos = fd ? fd->pkt_pos : -1;
                af->serial = state->audio.aud_decoder.pkt_serial;
                af->duration = av_q2d(AVRational{ frame->nb_samples, frame->sample_rate });

                av_frame_move_ref(af->frame, frame);
                state->audio.sampq.push();
                if (state->audio.audioq.serial() != state->audio.aud_decoder.pkt_serial)
                    break;
            }
            if (ret == AVERROR_EOF)
                state->audio.aud_decoder.finished = state->audio.aud_decoder.pkt_serial;
#else
            if (!(af = state->audio.sampq.peek_writable()))
                goto the_end;

            af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
            FrameData* fd = frame->opaque_ref ? reinterpret_cast<FrameData*>(frame->opaque_ref->data) : nullptr;
            af->pos = fd ? fd->pkt_pos : -1;
            af->serial = state->audio.aud_decoder.pkt_serial;
            af->duration = av_q2d({ frame->nb_samples, frame->sample_rate });

            av_frame_move_ref(af->frame, frame);
            state->audio.sampq.push();
#endif
        }
    } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
the_end:

    ReportDecoderFailure(state, state->audio.audioq, ret);
    av_frame_free(&frame);
    return ret;
}

int DecoderWorkers::Video(void* opaque)
{
    VideoState* state = (VideoState*)opaque;
    AVFrame* frame = av_frame_alloc();
    double pts;
    double duration;
    int ret;
    AVRational tb = state->video.video_st->time_base;
    AVRational frame_rate = av_guess_frame_rate(state->session.ic, state->video.video_st, NULL);
    AVFilterGraph* graph = NULL;
    AVFilterContext* filt_out = NULL, * filt_in = NULL;
    int last_w = 0;
    int last_h = 0;
    enum AVPixelFormat last_format = AVPixelFormat(-2);
    int last_serial = -1;
    int last_vfilter_idx = 0;
    if (!frame)
    {
        ReportDecoderFailure(state, state->video.videoq, AVERROR(ENOMEM));
        return AVERROR(ENOMEM);
    }
    StepScanCollector stepScan;

    for (;;) {
        ret = GetVideoFrame(state, frame);
        if (ret < 0)
            goto the_end;
        if (!ret) {
            if (state->session.step.scan.load(std::memory_order_acquire) &&
                state->video.vid_decoder.finished == state->session.step.serial.load(std::memory_order_acquire) &&
                state->video.vid_decoder.finished == state->video.videoq.serial()) {
                if (stepScan.scanSerial != state->video.vid_decoder.finished) {
                    stepScan.scanSerial = state->video.vid_decoder.finished;
                    stepScan.valid = false;
                    stepScan.finalized = false;
                }
                ret = FinishStepScan(state, stepScan);
                if (ret < 0)
                    goto the_end;
            }
            continue;
        }
#if CONFIG_AVFILTER
        if (last_w != frame->width ||
            last_h != frame->height ||
            last_format != frame->format ||
            last_serial != state->video.vid_decoder.pkt_serial ||
            last_vfilter_idx != state->filters.vfilter_idx)
        {
            av_log(NULL, AV_LOG_DEBUG,
                "Video frame changed from size:%dx%d format:%s serial:%d to size:%dx%d format:%s serial:%d\n",
                last_w, last_h,
                (const char*)av_x_if_null(av_get_pix_fmt_name(last_format), "none"), last_serial,
                frame->width, frame->height,
                (const char*)av_x_if_null(av_get_pix_fmt_name(AVPixelFormat(frame->format)), "none"), state->video.vid_decoder.pkt_serial);
            avfilter_graph_free(&graph);
            graph = avfilter_graph_alloc();
            if (!graph)
            {
                ret = AVERROR(ENOMEM);
                goto the_end;
            }
            graph->nb_threads = 0;
            const auto currentSpeed = state->audio.play_rate.load(std::memory_order_acquire);
            std::string mvfilters = "setpts=" + std::to_string(1 / currentSpeed) + "*PTS";
            if ((ret = ConfigureVideoFilters(graph, state, mvfilters.c_str(), frame, true)) < 0)
            {
                goto the_end;
            }
            filt_in = state->filters.in_video_filter;
            filt_out = state->filters.out_video_filter;
            last_w = frame->width;
            last_h = frame->height;
            last_format = AVPixelFormat(frame->format);
            last_serial = state->video.vid_decoder.pkt_serial;
            last_vfilter_idx = state->filters.vfilter_idx;
            frame_rate = av_buffersink_get_frame_rate(filt_out);
        }

        ret = av_buffersrc_add_frame(filt_in, frame);
        if (ret < 0)
            goto the_end;

        while (ret >= 0)
        {
            FrameData* fd;

            state->video.frame_last_returned_time = av_gettime_relative() / 1000000.0;

            ret = av_buffersink_get_frame_flags(filt_out, frame, 0);
            if (ret < 0)
            {
                if (ret == AVERROR_EOF)
                    state->video.vid_decoder.finished = state->video.vid_decoder.pkt_serial;
                else if (ret != AVERROR(EAGAIN))
                    goto the_end;
                ret = 0;
                break;
            }

            fd = frame->opaque_ref ? (FrameData*)frame->opaque_ref->data : NULL;

            state->video.frame_last_filter_delay = av_gettime_relative() / 1000000.0 - state->video.frame_last_returned_time;
            if (fabs(state->video.frame_last_filter_delay) > AV_NOSYNC_THRESHOLD / 10.0)
                state->video.frame_last_filter_delay = 0;
            tb = av_buffersink_get_time_base(filt_out);
            duration = (frame_rate.num && frame_rate.den ? av_q2d(AVRational{ frame_rate.den, frame_rate.num }) : 0);
            pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
            ret = CollectStepFrame(state, stepScan, frame, pts, duration, fd ? fd->pkt_pos : -1,
                state->video.vid_decoder.pkt_serial);
            av_frame_unref(frame);
            if (state->video.videoq.serial() != state->video.vid_decoder.pkt_serial)
                break;
        }
#else

        duration = (frame_rate.num && frame_rate.den ? av_q2d({ frame_rate.den, frame_rate.num }) : 0);
        pts = ((frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb));
        FrameData* fd = frame->opaque_ref ? reinterpret_cast<FrameData*>(frame->opaque_ref->data) : nullptr;
        ret = CollectStepFrame(state, stepScan, frame, pts, duration, fd ? fd->pkt_pos : -1,
            state->video.vid_decoder.pkt_serial);
        av_frame_unref(frame);
#endif
        if (ret < 0)
            goto the_end;
    }
the_end:

    ReportDecoderFailure(state, state->video.videoq, ret);
    av_frame_free(&stepScan.candidate);
    av_frame_free(&frame);
    avfilter_graph_free(&graph);
    return ret;
}

int DecoderWorkers::Subtitle(void* opaque)
{
    VideoState* state = (VideoState*)opaque;
    Frame* sp;
    int got_subtitle;
    double pts;
    int ret = 0;

    for (;;) {
        if (!(sp = state->subtitle.subpq.peek_writable()))
            return 0;

        AVSubtitle subtitle{};
        if ((got_subtitle = state->subtitle.sub_decoder.decode_frame(NULL, &subtitle)) < 0) {
            ret = got_subtitle;
            break;
        }

        pts = 0;

        if (got_subtitle) {
            if (subtitle.pts != AV_NOPTS_VALUE)
                pts = subtitle.pts / (double)AV_TIME_BASE;
            auto frame = SubtitleDispatcher::Copy(subtitle, pts);
            if (state->session.ic && state->session.ic->start_time != AV_NOPTS_VALUE)
                frame->timestampOffsetMs = state->session.ic->start_time / 1000;
            const auto* context = state->subtitle.sub_decoder.avctx;
            if (context->subtitle_header && context->subtitle_header_size > 0)
                frame->assHeader.assign(reinterpret_cast<const char*>(context->subtitle_header), context->subtitle_header_size);
            sp->subtitle = std::move(frame);
            avsubtitle_free(&subtitle);
            sp->pts = sp->subtitle->startSeconds;
            sp->serial = state->subtitle.sub_decoder.pkt_serial;
            sp->width = state->subtitle.sub_decoder.avctx->width;
            sp->height = state->subtitle.sub_decoder.avctx->height;
            state->subtitle.subpq.push();
        }
    }
    ReportDecoderFailure(state, state->subtitle.subtitleq, ret);
    return ret;
}
