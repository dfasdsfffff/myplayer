#include "filter_configurator.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

enum AVPixelFormat video_filter_pix_fmts[] = {
    AV_PIX_FMT_BGRA,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NONE,
};

enum AVColorSpace sdl_supported_color_spaces[] = {
    AVCOL_SPC_BT709,
    AVCOL_SPC_BT470BG,
    AVCOL_SPC_SMPTE170M,
    AVCOL_SPC_UNSPECIFIED,
};

double GetRotation(const int32_t* displaymatrix)
{
    double theta = 0;
    if (displaymatrix)
        theta = -round(av_display_rotation_get(displaymatrix));

    theta -= 360 * floor(theta / 360 + 0.9 / 360);

    if (fabs(theta - 90 * round(theta / 90)) > 2)
        av_log(NULL, AV_LOG_WARNING, "Odd rotation angle.\n"
            "If you want to help, upload a sample "
            "of this file to https://streams.videolan.org/upload/ "
            "and contact the ffmpeg-devel mailing list. (ffmpeg-devel@ffmpeg.org)");

    return theta;
}

} // namespace

int ConfigureFilterGraph(AVFilterGraph* graph, const char* filtergraph, AVFilterContext* sourceCtx, AVFilterContext* sinkCtx)
{
    int ret, i;
    int nb_filters = graph->nb_filters;
    AVFilterInOut* outputs = NULL, * inputs = NULL;

    if (filtergraph)
    {
        outputs = avfilter_inout_alloc();
        inputs = avfilter_inout_alloc();
        if (!outputs || !inputs)
        {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        outputs->name = av_strdup("in");
        outputs->filter_ctx = sourceCtx;
        outputs->pad_idx = 0;
        outputs->next = NULL;

        inputs->name = av_strdup("out");
        inputs->filter_ctx = sinkCtx;
        inputs->pad_idx = 0;
        inputs->next = NULL;

        if ((ret = avfilter_graph_parse_ptr(graph, filtergraph, &inputs, &outputs, NULL)) < 0)
            goto fail;
    }
    else
    {
        if ((ret = avfilter_link(sourceCtx, 0, sinkCtx, 0)) < 0)
            goto fail;
    }

    /* Reorder the filters to ensure that inputs of the custom filters are merged first */
    for (i = 0; i < graph->nb_filters - nb_filters; i++)
        FFSWAP(AVFilterContext*, graph->filters[i], graph->filters[i + nb_filters]);

    ret = avfilter_graph_config(graph, NULL);
fail:
    avfilter_inout_free(&outputs);
    avfilter_inout_free(&inputs);
    return ret;
}

int ConfigureVideoFilters(AVFilterGraph* graph, VideoState* state, const char* filters, AVFrame* frame, bool autorotate)
{
    // char sws_flags_str[512] = "";
    char buffersrc_args[256];
    int ret;
    AVFilterContext* filt_src = NULL, * filt_out = NULL, * last_filter = NULL;
    AVCodecParameters* codecpar = state->video.video_st->codecpar;
    AVRational fr = av_guess_frame_rate(state->session.ic, state->video.video_st, NULL);
    const AVDictionaryEntry* e = NULL;
    AVBufferSrcParameters* par = av_buffersrc_parameters_alloc();

    if (!par)
        return AVERROR(ENOMEM);
//while ((e = av_dict_iterate(sws_dict, e)))
    //{
    //	if (!strcmp(e->key, "sws_flags"))
    //	{
    //		av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", "flags", e->value);
    //	}
    //	else
    //		av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", e->key, e->value);
    //}
    //if (strlen(sws_flags_str))
    //	sws_flags_str[strlen(sws_flags_str) - 1] = '\0';

    //graph->scale_sws_opts = av_strdup(sws_flags_str);

    snprintf(buffersrc_args, sizeof(buffersrc_args),
        "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d:"
        "colorspace=%d:range=%d",
        frame->width, frame->height, frame->format,
        state->video.video_st->time_base.num, state->video.video_st->time_base.den,
        codecpar->sample_aspect_ratio.num, FFMAX(codecpar->sample_aspect_ratio.den, 1),
        frame->colorspace, frame->color_range);
    if (fr.num && fr.den)
        av_strlcatf(buffersrc_args, sizeof(buffersrc_args), ":frame_rate=%d/%d", fr.num, fr.den);

    if ((ret = avfilter_graph_create_filter(&filt_src,
        avfilter_get_by_name("buffer"),
        "ffplay_buffer", buffersrc_args, NULL,
        graph)) < 0)
        goto fail;
    par->hw_frames_ctx = frame->hw_frames_ctx;
    ret = av_buffersrc_parameters_set(filt_src, par);
    if (ret < 0)
        goto fail;

    filt_out = avfilter_graph_alloc_filter(graph, avfilter_get_by_name("buffersink"),
        "ffplay_buffersink");
    if (!filt_out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if ((ret = av_opt_set_array(filt_out, "pixel_formats", AV_OPT_SEARCH_CHILDREN, 0,
        FF_ARRAY_ELEMS(video_filter_pix_fmts) - 1, AV_OPT_TYPE_PIXEL_FMT, video_filter_pix_fmts)) < 0)
        goto fail;
    if ((ret = av_opt_set_array(filt_out, "colorspaces", AV_OPT_SEARCH_CHILDREN, 0,
        FF_ARRAY_ELEMS(sdl_supported_color_spaces) - 1, AV_OPT_TYPE_INT, sdl_supported_color_spaces)) < 0)
        goto fail;

    ret = avfilter_init_dict(filt_out, NULL);
    if (ret < 0)
        goto fail;

    last_filter = filt_out;

    /* Note: this macro adds a filter before the lastly added filter, so the
     * processing order of the filters is in reverse */
#define INSERT_FILT(name, arg)                                                \
    do                                                                        \
    {                                                                         \
        AVFilterContext *filt_ctx;                                            \
                                                                              \
        ret = avfilter_graph_create_filter(&filt_ctx,                         \
                                           avfilter_get_by_name(name),        \
                                           "ffplay_" name, arg, NULL, graph); \
        if (ret < 0)                                                          \
            goto fail;                                                        \
                                                                              \
        ret = avfilter_link(filt_ctx, 0, last_filter, 0);                     \
        if (ret < 0)                                                          \
            goto fail;                                                        \
                                                                              \
        last_filter = filt_ctx;                                               \
    } while (0)

    if (autorotate)
    {
        double theta = 0.0;
        int32_t* displaymatrix = NULL;
        AVFrameSideData* sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DISPLAYMATRIX);
        if (sd)
            displaymatrix = (int32_t*)sd->data;
        if (!displaymatrix)
        {
            const AVPacketSideData* psd = av_packet_side_data_get(state->video.video_st->codecpar->coded_side_data,
                state->video.video_st->codecpar->nb_coded_side_data,
                AV_PKT_DATA_DISPLAYMATRIX);
            if (psd)
                displaymatrix = (int32_t*)psd->data;
        }
        theta = GetRotation(displaymatrix);

        if (fabs(theta - 90) < 1.0)
        {
            INSERT_FILT("transpose", displaymatrix[3] > 0 ? "cclock_flip" : "clock");
        }
        else if (fabs(theta - 180) < 1.0)
        {
            if (displaymatrix[0] < 0)
                INSERT_FILT("hflip", NULL);
            if (displaymatrix[4] < 0)
                INSERT_FILT("vflip", NULL);
        }
        else if (fabs(theta - 270) < 1.0)
        {
            INSERT_FILT("transpose", displaymatrix[3] < 0 ? "clock_flip" : "cclock");
        }
        else if (fabs(theta) > 1.0)
        {
            char rotate_buf[64];
            snprintf(rotate_buf, sizeof(rotate_buf), "%f*PI/180", theta);
            INSERT_FILT("rotate", rotate_buf);
        }
        else
        {
            if (displaymatrix && displaymatrix[4] < 0)
                INSERT_FILT("vflip", NULL);
        }
    }

    if ((ret = ConfigureFilterGraph(graph, filters, filt_src, last_filter)) < 0)
        goto fail;

    state->filters.in_video_filter = filt_src;
    state->filters.out_video_filter = filt_out;

fail:
    av_freep(&par);
    return ret;
}

int ConfigureAudioFilters(VideoState* state, const char* filters, bool forceOutputFormat)
{
    AVFilterContext* filt_asrc = NULL, * filt_asink = NULL;
    char aresample_swr_opts[512] = "";
    const AVDictionaryEntry* e = NULL;
    AVBPrint bp;
    char asrc_args[256];
    int ret;

    avfilter_graph_free(&state->filters.agraph);
    if (!(state->filters.agraph = avfilter_graph_alloc()))
        return AVERROR(ENOMEM);
    state->filters.agraph->nb_threads = 0;

    av_bprint_init(&bp, 0, AV_BPRINT_SIZE_AUTOMATIC);

    //while ((e = av_dict_iterate(swr_opts, e)))
    //	av_strlcatf(aresample_swr_opts, sizeof(aresample_swr_opts), "%s=%s:", e->key, e->value);
    //if (strlen(aresample_swr_opts))
    //	aresample_swr_opts[strlen(aresample_swr_opts) - 1] = '\0';
    //av_opt_set(is->filters.agraph, "aresample_swr_opts", aresample_swr_opts, 0);

    av_channel_layout_describe_bprint(&state->audio.audio_filter_src.ch_layout, &bp);

    ret = snprintf(asrc_args, sizeof(asrc_args),
        "sample_rate=%d:sample_fmt=%s:time_base=%d/%d:channel_layout=%s",
        state->audio.audio_filter_src.freq, av_get_sample_fmt_name(state->audio.audio_filter_src.fmt),
        1, state->audio.audio_filter_src.freq, bp.str);
    // 创建音频源滤镜
    ret = avfilter_graph_create_filter(&filt_asrc,
        avfilter_get_by_name("abuffer"), "ffplay_abuffer",
        asrc_args, NULL, state->filters.agraph);
    if (ret < 0)
        goto end;
    // 创建音频汇滤镜
    filt_asink = avfilter_graph_alloc_filter(state->filters.agraph, avfilter_get_by_name("abuffersink"),
        "ffplay_abuffersink");
    if (!filt_asink) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if ((ret = av_opt_set(filt_asink, "sample_formats", "s16", AV_OPT_SEARCH_CHILDREN)) < 0)
        goto end;

    if (forceOutputFormat)
    {
        if ((ret = av_opt_set_array(filt_asink, "channel_layouts", AV_OPT_SEARCH_CHILDREN,
            0, 1, AV_OPT_TYPE_CHLAYOUT, &state->audio.audio_tgt.ch_layout)) < 0)
            goto end;
        if ((ret = av_opt_set_array(filt_asink, "samplerates", AV_OPT_SEARCH_CHILDREN,
            0, 1, AV_OPT_TYPE_INT, &state->audio.audio_tgt.freq)) < 0)
            goto end;
    }

    ret = avfilter_init_dict(filt_asink, NULL);
    if (ret < 0)
        goto end;
    {

        std::string s = filters;
        if ((ret = ConfigureFilterGraph(state->filters.agraph, filters, filt_asrc, filt_asink)) < 0)
            goto end;
    }

    state->filters.in_audio_filter = filt_asrc;
    state->filters.out_audio_filter = filt_asink;

end:
    if (ret < 0)
        avfilter_graph_free(&state->filters.agraph);
    av_bprint_finalize(&bp, NULL);

    return ret;
}
