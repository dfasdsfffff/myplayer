#define SDL_MAIN_HANDLED

#include "video_frame_converter.h"

#include <iostream>
#include <memory>

namespace {
bool Expect(bool condition, const char* message) { if (!condition) std::cerr << "FAILED: " << message << '\n'; return condition; }
struct FrameDeleter { void operator()(AVFrame* frame) const { av_frame_free(&frame); } };
}

int main()
{
    std::unique_ptr<AVFrame, FrameDeleter> source(av_frame_alloc());
    if (!Expect(source != nullptr, "source frame allocates")) return 1;
    source->format = AV_PIX_FMT_YUV420P;
    source->width = 4;
    source->height = 2;
    if (!Expect(av_frame_get_buffer(source.get(), 32) == 0, "source planes allocate")) return 1;
    const int yStride = source->linesize[0], uStride = source->linesize[1], vStride = source->linesize[2];

    VideoFrameConverter converter;
    const auto frame = converter.convert(source.get());
    if (!Expect(frame && frame->format == VideoFrameFormat::Yuv420P, "even YUV420P stays on the YUV path")) return 1;
    if (!Expect(frame->planes[0].stride == yStride && frame->planes[1].stride == uStride && frame->planes[2].stride == vStride,
            "YUV plane strides are preserved")) return 1;
    if (!Expect(frame->planes[0].height == 2 && frame->planes[1].height == 1 && frame->planes[2].height == 1,
            "YUV plane heights match 4:2:0 geometry")) return 1;
    av_frame_unref(source.get());
    if (!Expect(frame->planes[0].data && frame->planes[1].data && frame->planes[2].data,
            "YUV frame retains its referenced source buffers after decoder reuse")) return 1;

    source->format = AV_PIX_FMT_YUV420P;
    source->width = 3;
    source->height = 3;
    if (!Expect(av_frame_get_buffer(source.get(), 32) == 0, "odd source allocates")) return 1;
    const auto oddFrame = converter.convert(source.get());
    if (!Expect(oddFrame && oddFrame->format == VideoFrameFormat::Bgra32 && !oddFrame->bgra.empty(),
            "odd-sized YUV falls back to compatible BGRA")) return 1;
    return 0;
}
