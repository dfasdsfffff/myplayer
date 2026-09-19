#define SDL_MAIN_HANDLED

#include "hardware_decode.h"

#include <iostream>

namespace {
bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

int main()
{
    if (!Expect(!ShouldAttemptHardwareDecode(HardwareDecodePreference::Disabled, true),
                "disabled preference must not attempt hardware decoding"))
        return 1;
    if (!Expect(ShouldAttemptHardwareDecode(HardwareDecodePreference::Auto, true),
                "auto preference should use an available D3D11VA backend"))
        return 1;
    if (!Expect(!ShouldAttemptHardwareDecode(HardwareDecodePreference::Auto, false),
                "auto preference must fall back when D3D11VA is unavailable"))
        return 1;
    if (!Expect(!ShouldAttemptHardwareDecode(HardwareDecodePreference::D3D11VA, false),
                "explicit D3D11VA must still fall back when unavailable"))
        return 1;
    if (!Expect(HardwareDecodeFallbackReason(HardwareDecodePreference::Disabled, true) == "disabled by preference",
                "disabled preference reports its fallback reason"))
        return 1;
    if (!Expect(HardwareDecodeFallbackReason(HardwareDecodePreference::Auto, false) == "D3D11VA unavailable",
                "unavailable auto reports a diagnostic fallback reason"))
        return 1;
    if (!Expect(ChooseHardwarePixelFormat({AV_PIX_FMT_YUV420P, AV_PIX_FMT_D3D11, AV_PIX_FMT_NONE}) == AV_PIX_FMT_D3D11,
                "D3D11 pixel format is selected when offered"))
        return 1;
    if (!Expect(ChooseHardwarePixelFormat({AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE}) == AV_PIX_FMT_NONE,
                "missing hardware pixel format forces software fallback"))
        return 1;

    AVCodecContext* codecContext = avcodec_alloc_context3(nullptr);
    if (!Expect(codecContext != nullptr, "test allocates a codec context"))
        return 1;
    codecContext->hw_device_ctx = av_buffer_alloc(1);
    codecContext->opaque = reinterpret_cast<void*>(1);
    codecContext->get_format = [](AVCodecContext*, const AVPixelFormat*) { return AV_PIX_FMT_D3D11; };
    HardwareDecodeContext context;
    const HardwareDecodeResult disabled = context.configure(codecContext, HardwareDecodePreference::Disabled);
    if (!Expect(!disabled.active && disabled.fallbackReason == "disabled by preference",
                "disabled preference reports software fallback") ||
        !Expect(codecContext->hw_device_ctx == nullptr && codecContext->opaque == nullptr &&
                    codecContext->get_format == avcodec_default_get_format,
                "disabled preference restores FFmpeg's default pixel-format callback")) {
        avcodec_free_context(&codecContext);
        return 1;
    }
    avcodec_free_context(&codecContext);
    return 0;
}
