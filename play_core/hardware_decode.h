#pragma once

#include "av_compat.h"
#include "playback_settings.h"

#include <initializer_list>
#include <string>

struct HardwareDecodeResult {
    bool active{false};
    std::string backend;
    std::string fallbackReason;
};

bool ShouldAttemptHardwareDecode(HardwareDecodePreference preference, bool d3d11vaAvailable);
std::string HardwareDecodeFallbackReason(HardwareDecodePreference preference, bool d3d11vaAvailable);
AVPixelFormat ChooseHardwarePixelFormat(std::initializer_list<AVPixelFormat> formats);

class HardwareDecodeContext final {
public:
    HardwareDecodeContext() = default;
    ~HardwareDecodeContext();

    HardwareDecodeContext(const HardwareDecodeContext&) = delete;
    HardwareDecodeContext& operator=(const HardwareDecodeContext&) = delete;

    HardwareDecodeResult configure(AVCodecContext* codecContext, HardwareDecodePreference preference);
    AVFrame* transferToSoftware(const AVFrame* hardwareFrame, AVFrame* reusable);
    [[nodiscard]] bool isHardwareFrame(const AVFrame* frame) const noexcept;
    [[nodiscard]] const HardwareDecodeResult& result() const noexcept;

private:
    static AVPixelFormat getFormat(AVCodecContext* codecContext, const AVPixelFormat* formats);
    void disable(std::string reason);

    AVBufferRef* m_deviceContext{nullptr};
    AVPixelFormat m_hardwarePixelFormat{AV_PIX_FMT_NONE};
    HardwareDecodeResult m_result;
};
