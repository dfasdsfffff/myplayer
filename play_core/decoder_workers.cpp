#include "decoder_workers.h"

#include "filter_configurator.h"
#include "media_sync.h"

#include <cmath>
#include <format>
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

int DecoderWorkers::GetVideoFrame(VideoState* state, AVFrame* frame)
{
    int got_picture;

    if ((got_picture = state->video.vid_decoder.decode_frame(frame, NULL)) < 0)
        return -1;

    if (got_picture) {
        double dpts = NAN;

        if (frame->pts != AV_NOPTS_VALUE)
            dpts = av_q2d(state->video.video_st->time_base) * frame->pts;
        frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(state->session.ic, state->video.video_st, frame);

        if (framedrop > 0
            || (framedrop && MediaSync::get_master_sync_type(state) != AV_SYNC_VIDEO_MASTER)
            ) {
            if (frame->pts != AV_NOPTS_VALUE) {
                double diff = dpts - MediaSync::get_master_clock(state);
                if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
                    diff - state->video.frame_last_filter_delay < 0 &&
					state->video.vid_decoder.pkt_serial == state->clocks.vidclk.serial() &&
                    state->video.videoq.nb_packets) {
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

    if (!frame)
        return AVERROR(ENOMEM);

    do {
        if ((got_frame = state->audio.aud_decoder.decode_frame(frame, NULL)) < 0)
            goto the_end;

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
                auto afilters = std::format("atempo={:.2f}", currentSpeed);
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
                if (state->audio.audioq.serial.load(std::memory_order_acquire) != state->audio.aud_decoder.pkt_serial)
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
        return AVERROR(ENOMEM);
    }

    for (;;) {
        ret = GetVideoFrame(state, frame);
        if (ret < 0)
            goto the_end;
        if (!ret)
            continue;
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
            std::string mvfilters = std::format("setpts={:.2f}*PTS", 1 / currentSpeed);
            if ((ret = ConfigureVideoFilters(graph, state, mvfilters.c_str(), frame, true)) < 0)
            {
                state->session.stop_refresh_loop.store(true, std::memory_order_release);
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
            ret = QueuePicture(state, frame, pts, duration, fd ? fd->pkt_pos : -1, state->video.vid_decoder.pkt_serial);
            av_frame_unref(frame);
            if (state->video.videoq.serial.load(std::memory_order_acquire) != state->video.vid_decoder.pkt_serial)
                break;
        }
#else

        duration = (frame_rate.num && frame_rate.den ? av_q2d({ frame_rate.den, frame_rate.num }) : 0);
        pts = ((frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb));
        FrameData* fd = frame->opaque_ref ? reinterpret_cast<FrameData*>(frame->opaque_ref->data) : nullptr;
        ret = QueuePicture(state, frame, pts, duration, fd ? fd->pkt_pos : -1, state->video.vid_decoder.pkt_serial);
        av_frame_unref(frame);
#endif
        if (ret < 0)
            goto the_end;
    }
the_end:

    av_frame_free(&frame);
    avfilter_graph_free(&graph);
    return 0;
}

int DecoderWorkers::Subtitle(void* opaque)
{
    VideoState* state = (VideoState*)opaque;
    Frame* sp;
    int got_subtitle;
    double pts;

    for (;;) {
        if (!(sp = state->subtitle.subpq.peek_writable()))
            return 0;

        if ((got_subtitle = state->subtitle.sub_decoder.decode_frame(NULL, &sp->sub)) < 0)
            break;

        pts = 0;

        if (got_subtitle && sp->sub.format == 0) {
            if (sp->sub.pts != AV_NOPTS_VALUE)
                pts = sp->sub.pts / (double)AV_TIME_BASE;
            sp->pts = pts;
            sp->serial = state->subtitle.sub_decoder.pkt_serial;
            sp->width = state->subtitle.sub_decoder.avctx->width;
            sp->height = state->subtitle.sub_decoder.avctx->height;
            state->subtitle.subpq.push();
        }
        else if (got_subtitle) {
            avsubtitle_free(&sp->sub);
        }
    }
    return 0;
}
