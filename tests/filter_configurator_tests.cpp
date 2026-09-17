#define SDL_MAIN_HANDLED

#include "filter_configurator.h"

#include "video_state.h"

#include <iostream>
#include <type_traits>

extern "C" {
#include <libavfilter/avfilter.h>
#if CONFIG_AVFILTER
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#endif
}

namespace {

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

} // namespace

int main()
{
    static_assert(std::is_same_v<decltype(&ConfigureFilterGraph),
        int (*)(AVFilterGraph*, const char*, AVFilterContext*, AVFilterContext*)>);
    static_assert(std::is_same_v<decltype(&ConfigureVideoFilters),
        int (*)(AVFilterGraph*, VideoState*, const char*, AVFrame*, bool)>);
    static_assert(std::is_same_v<decltype(&ConfigureAudioFilters),
        int (*)(VideoState*, const char*, bool)>);

#if CONFIG_AVFILTER
    AVFilterGraph* graph = avfilter_graph_alloc();
    if (!Expect(graph != nullptr, "filter graph should allocate"))
        return 1;

    const AVFilter* buffer = avfilter_get_by_name("buffer");
    const AVFilter* buffersink = avfilter_get_by_name("buffersink");
    if (!Expect(buffer != nullptr && buffersink != nullptr, "test filters should exist")) {
        avfilter_graph_free(&graph);
        return 1;
    }

    AVFilterContext* sourceCtx = nullptr;
    AVFilterContext* sinkCtx = nullptr;
    const char* args = "video_size=2x2:pix_fmt=0:time_base=1/25:pixel_aspect=1/1";
    int ret = avfilter_graph_create_filter(&sourceCtx, buffer, "test_src", args, nullptr, graph);
    if (!Expect(ret >= 0, "buffer source should create")) {
        avfilter_graph_free(&graph);
        return 1;
    }
    ret = avfilter_graph_create_filter(&sinkCtx, buffersink, "test_sink", nullptr, nullptr, graph);
    if (!Expect(ret >= 0, "buffer sink should create")) {
        avfilter_graph_free(&graph);
        return 1;
    }

    ret = ConfigureFilterGraph(graph, "null", sourceCtx, sinkCtx);
    if (!Expect(ret >= 0, "generic filter graph connector should configure")) {
        avfilter_graph_free(&graph);
        return 1;
    }

    avfilter_graph_free(&graph);
#endif

    return 0;
}
