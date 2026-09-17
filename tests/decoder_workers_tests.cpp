#define SDL_MAIN_HANDLED

#include "decoder_workers.h"

#include "video_state.h"

#include <iostream>
#include <memory>
#include <type_traits>

namespace {

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

struct AvFrameDeleter {
    void operator()(AVFrame* frame) const
    {
        av_frame_free(&frame);
    }
};

struct AvCodecContextDeleter {
    void operator()(AVCodecContext* context) const
    {
        avcodec_free_context(&context);
    }
};

struct SdlCondDeleter {
    void operator()(SDL_cond* condition) const
    {
        if (condition)
            SDL_DestroyCond(condition);
    }
};

} // namespace

int main()
{
    static_assert(std::is_same_v<decltype(&DecoderWorkers::Audio), int (*)(void*)>);
    static_assert(std::is_same_v<decltype(&DecoderWorkers::Video), int (*)(void*)>);
    static_assert(std::is_same_v<decltype(&DecoderWorkers::Subtitle), int (*)(void*)>);

    {
        auto state = std::make_unique<VideoState>();
        if (!Expect(!state->session.stop_refresh_loop.load(std::memory_order_acquire),
                "decoder worker stop-refresh request defaults to false"))
            return 1;
        state->session.stop_refresh_loop.store(true, std::memory_order_release);
        if (!Expect(state->session.stop_refresh_loop.load(std::memory_order_acquire),
                "decoder worker can request refresh loop stop through VideoState"))
            return 1;
    }

    {
        auto state = std::make_unique<VideoState>();
        if (!Expect(state->video.videoq.init() == 0, "video packet queue should initialize"))
            return 1;
        std::unique_ptr<SDL_cond, SdlCondDeleter> emptyQueue(SDL_CreateCond());
        if (!Expect(emptyQueue != nullptr, "decoder condition should initialize"))
            return 1;
        std::unique_ptr<AVCodecContext, AvCodecContextDeleter> context(avcodec_alloc_context3(nullptr));
        if (!Expect(context != nullptr, "codec context should allocate"))
            return 1;
        context->codec_type = AVMEDIA_TYPE_VIDEO;
        if (!Expect(state->video.vid_decoder.init(context.release(), &state->video.videoq, emptyQueue.get()) == 0,
                "video decoder should initialize"))
            return 1;
        state->video.videoq.abort();

        std::unique_ptr<AVFrame, AvFrameDeleter> frame(av_frame_alloc());
        if (!Expect(frame != nullptr, "video frame should allocate"))
            return 1;
        if (!Expect(DecoderWorkers::GetVideoFrame(state.get(), frame.get()) == -1,
                "aborted empty video decoder queue returns existing error result"))
            return 1;
    }

    {
        auto state = std::make_unique<VideoState>();
        if (!Expect(state->video.videoq.init() == 0, "picture packet queue should initialize"))
            return 1;
        if (!Expect(state->video.pictq.init(&state->video.videoq, 1, 0) == 0,
                "picture frame queue should initialize"))
            return 1;
        state->video.videoq.abort();

        std::unique_ptr<AVFrame, AvFrameDeleter> source(av_frame_alloc());
        if (!Expect(source != nullptr, "source video frame should allocate"))
            return 1;
        source->sample_aspect_ratio = {1, 1};
        source->width = 2;
        source->height = 2;
        source->format = AV_PIX_FMT_BGRA;
        if (!Expect(DecoderWorkers::QueuePicture(state.get(), source.get(), 1.0, 0.04, 123, 7) == -1,
                "aborted picture queue returns existing abort result"))
            return 1;
    }

    return 0;
}
