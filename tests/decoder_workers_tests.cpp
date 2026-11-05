#define SDL_MAIN_HANDLED

#include "decoder_workers.h"

#include "video_state.h"

#include <cerrno>
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
    if (!Expect(Decoder::is_recoverable_error(AVERROR_INVALIDDATA),
            "invalid packet errors are recoverable at the decoder boundary"))
        return 1;
    if (!Expect(!Decoder::is_recoverable_error(AVERROR(EINVAL)),
            "non-invalid-data decoder errors remain fatal"))
        return 1;

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
        if (!Expect(state->session.decoderError.load(std::memory_order_acquire) == 0,
                "decoder cancellation must not publish a failure"))
            return 1;
    }

    {
        auto state = std::make_unique<VideoState>();
        if (!Expect(state->audio.audioq.init() == 0, "audio packet queue should initialize"))
            return 1;
        state->audio.audioq.start();
        std::unique_ptr<SDL_cond, SdlCondDeleter> emptyQueue(SDL_CreateCond());
        if (!Expect(emptyQueue != nullptr, "audio decoder condition should initialize"))
            return 1;
        std::unique_ptr<AVCodecContext, AvCodecContextDeleter> context(avcodec_alloc_context3(nullptr));
        if (!Expect(context != nullptr, "audio codec context should allocate"))
            return 1;
        context->codec_type = AVMEDIA_TYPE_AUDIO;
        if (!Expect(state->audio.aud_decoder.init(context.release(), &state->audio.audioq, emptyQueue.get()) == 0,
                "audio decoder should initialize"))
            return 1;

        AVPacket packet{};
        if (!Expect(av_new_packet(&packet, 1) == 0, "invalid audio packet should allocate"))
            return 1;
        packet.data[0] = 0;
        if (!Expect(state->audio.audioq.put(&packet) == 0, "audio queue should accept invalid packet"))
            return 1;

        const int result = DecoderWorkers::Audio(state.get());
        if (!Expect(result < 0, "fatal decoder error should propagate from audio worker"))
            return 1;
        if (!Expect(state->session.decoderError.load(std::memory_order_acquire) < 0,
                "fatal decoder error should publish its negative error code"))
            return 1;
    }

    {
        auto state = std::make_unique<VideoState>();
        if (!Expect(state->audio.audioq.init() == 0, "cancelled audio queue should initialize"))
            return 1;
        std::unique_ptr<SDL_cond, SdlCondDeleter> emptyQueue(SDL_CreateCond());
        if (!Expect(emptyQueue != nullptr, "cancelled decoder condition should initialize"))
            return 1;
        std::unique_ptr<AVCodecContext, AvCodecContextDeleter> context(avcodec_alloc_context3(nullptr));
        if (!Expect(context != nullptr, "cancelled codec context should allocate"))
            return 1;
        context->codec_type = AVMEDIA_TYPE_AUDIO;
        if (!Expect(state->audio.aud_decoder.init(context.release(), &state->audio.audioq, emptyQueue.get()) == 0,
                "cancelled audio decoder should initialize"))
            return 1;
        state->audio.audioq.abort();

        const int result = DecoderWorkers::Audio(state.get());
        if (!Expect(result < 0, "cancelled audio worker should stop with existing error result"))
            return 1;
        if (!Expect(state->session.decoderError.load(std::memory_order_acquire) == 0,
                "cancelled audio worker must not publish decoder failure"))
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
