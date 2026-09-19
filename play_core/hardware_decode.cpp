#include "hardware_decode.h"

extern "C" {
#include <libavutil/hwcontext.h>
}

bool ShouldAttemptHardwareDecode(HardwareDecodePreference preference, bool d3d11vaAvailable)
{
    return preference != HardwareDecodePreference::Disabled && d3d11vaAvailable;
}

std::string HardwareDecodeFallbackReason(HardwareDecodePreference preference, bool d3d11vaAvailable)
{
    if (preference == HardwareDecodePreference::Disabled)
        return "disabled by preference";
    if (!d3d11vaAvailable)
        return "D3D11VA unavailable";
    return {};
}

AVPixelFormat ChooseHardwarePixelFormat(std::initializer_list<AVPixelFormat> formats)
{
    for (const AVPixelFormat format : formats) {
        if (format == AV_PIX_FMT_D3D11)
            return format;
    }
    return AV_PIX_FMT_NONE;
}

HardwareDecodeContext::~HardwareDecodeContext()
{
    av_buffer_unref(&m_deviceContext);
}

HardwareDecodeResult HardwareDecodeContext::configure(AVCodecContext* codecContext, HardwareDecodePreference preference)
{
    av_buffer_unref(&m_deviceContext);
    m_hardwarePixelFormat = AV_PIX_FMT_NONE;
    m_result = {};

    if (!codecContext) {
        disable("missing codec context");
        return m_result;
    }

#ifdef _WIN32
    const AVHWDeviceType deviceType = av_hwdevice_find_type_by_name("d3d11va");
    const bool available = deviceType != AV_HWDEVICE_TYPE_NONE;
    if (!ShouldAttemptHardwareDecode(preference, available)) {
        disable(HardwareDecodeFallbackReason(preference, available));
        return m_result;
    }

    const int createResult = av_hwdevice_ctx_create(&m_deviceContext, deviceType, nullptr, nullptr, 0);
    if (createResult < 0) {
        disable("D3D11VA device creation failed");
        return m_result;
    }
    codecContext->hw_device_ctx = av_buffer_ref(m_deviceContext);
    if (!codecContext->hw_device_ctx) {
        disable("D3D11VA device reference failed");
        return m_result;
    }
    codecContext->opaque = this;
    codecContext->get_format = &HardwareDecodeContext::getFormat;
    m_hardwarePixelFormat = AV_PIX_FMT_D3D11;
    m_result.active = true;
    m_result.backend = "D3D11VA";
#else
    (void)preference;
    disable("D3D11VA is only available on Windows");
#endif
    return m_result;
}

AVFrame* HardwareDecodeContext::transferToSoftware(const AVFrame* hardwareFrame, AVFrame* reusable)
{
    if (!hardwareFrame || !reusable || !isHardwareFrame(hardwareFrame))
        return nullptr;

    av_frame_unref(reusable);
    if (av_hwframe_transfer_data(reusable, hardwareFrame, 0) < 0 ||
        av_frame_copy_props(reusable, hardwareFrame) < 0) {
        av_frame_unref(reusable);
        disable("D3D11VA frame transfer failed");
        return nullptr;
    }
    return reusable;
}

bool HardwareDecodeContext::isHardwareFrame(const AVFrame* frame) const noexcept
{
    return m_result.active && frame && frame->format == m_hardwarePixelFormat;
}

const HardwareDecodeResult& HardwareDecodeContext::result() const noexcept
{
    return m_result;
}

AVPixelFormat HardwareDecodeContext::getFormat(AVCodecContext* codecContext, const AVPixelFormat* formats)
{
    auto* context = static_cast<HardwareDecodeContext*>(codecContext ? codecContext->opaque : nullptr);
    if (!context || !formats)
        return AV_PIX_FMT_NONE;
    for (const AVPixelFormat* format = formats; *format != AV_PIX_FMT_NONE; ++format) {
        if (*format == AV_PIX_FMT_D3D11)
            return *format;
    }
    context->disable("D3D11VA pixel format negotiation failed");
    return formats[0];
}

void HardwareDecodeContext::disable(std::string reason)
{
    m_result.active = false;
    m_result.backend.clear();
    m_result.fallbackReason = std::move(reason);
    m_hardwarePixelFormat = AV_PIX_FMT_NONE;
}
