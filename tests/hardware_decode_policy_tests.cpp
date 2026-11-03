#define SDL_MAIN_HANDLED

#include "hardware_decode.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include <libavutil/hwcontext.h>
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <libavutil/hwcontext_d3d11va.h>
#include <dxgi.h>
#endif

namespace {
bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

struct FormatDeleter {
    void operator()(AVFormatContext* value) const { avformat_close_input(&value); }
};
struct CodecDeleter {
    void operator()(AVCodecContext* value) const { avcodec_free_context(&value); }
};
struct FrameDeleter {
    void operator()(AVFrame* value) const { av_frame_free(&value); }
};
struct PacketDeleter {
    void operator()(AVPacket* value) const { av_packet_free(&value); }
};

struct AdapterObservation {
    bool available{false};
    std::string description;
    std::uint32_t vendorId{}, deviceId{}, subsystemId{}, revision{}, luidLow{};
    std::int32_t luidHigh{};
};

struct DecodeObservation {
    std::string media;
    std::string requestedHardware;
    std::string configuredBackend;
    std::string fallbackDiagnostic;
    std::string codec;
    std::string error;
    std::string adapterDiagnostic{"No hardware frame observed; no decoder-selected adapter is asserted"};
    AdapterObservation adapter;
    std::int64_t decodedFrames{}, hardwareFrames{}, softwareFrames{}, transferredFrames{};
    std::int64_t corruptPackets{}, corruptFrames{};
    int videoStreamIndex{-1}, declaredWidth{}, declaredHeight{};
    bool demuxEof{false}, decoderEof{false};
    std::vector<std::pair<int, int>> dimensions;
    std::vector<std::string> decodedFormats;
    std::vector<std::string> softwareFormats;
};

std::string AvError(int code)
{
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
    av_strerror(code, buffer.data(), buffer.size());
    return buffer.data();
}

void CheckAv(int code, const char* operation)
{
    if (code < 0)
        throw std::runtime_error(std::string(operation) + ": " + AvError(code));
}

void AddFormat(std::vector<std::string>& formats, int format)
{
    const char* name = av_get_pix_fmt_name(static_cast<AVPixelFormat>(format));
    if (!name)
        throw std::runtime_error("Decoder returned an unknown pixel format");
    if (std::find(formats.begin(), formats.end(), name) == formats.end())
        formats.emplace_back(name);
}

void ValidateSoftwareFrame(const AVFrame* frame)
{
    if (!frame || frame->width <= 0 || frame->height <= 0)
        throw std::runtime_error("Decoded software frame has invalid dimensions");
    CheckAv(av_image_check_size(frame->width, frame->height, 0, nullptr), "Frame dimensions");
    const auto format = static_cast<AVPixelFormat>(frame->format);
    const AVPixFmtDescriptor* descriptor = av_pix_fmt_desc_get(format);
    if (!descriptor || (descriptor->flags & AV_PIX_FMT_FLAG_HWACCEL))
        throw std::runtime_error("Expected a CPU-accessible decoded frame");
    int minimumLines[4]{};
    CheckAv(av_image_fill_linesizes(minimumLines, format, frame->width), "Frame plane layout");
    std::ptrdiff_t lines[4]{};
    for (int plane = 0; plane < 4; ++plane) {
        const auto stride = static_cast<std::int64_t>(frame->linesize[plane]);
        lines[plane] = static_cast<std::ptrdiff_t>(stride < 0 ? -stride : stride);
        if (lines[plane] < minimumLines[plane])
            throw std::runtime_error("Decoded frame has an undersized plane stride");
    }
    std::size_t sizes[4]{};
    CheckAv(av_image_fill_plane_sizes(sizes, format, frame->height, lines), "Frame plane sizes");
    for (int plane = 0; plane < 4; ++plane) {
        if (!sizes[plane])
            continue;
        const AVBufferRef* buffer = av_frame_get_plane_buffer(const_cast<AVFrame*>(frame), plane);
        if (!frame->data[plane] || !buffer)
            throw std::runtime_error("Decoded frame has missing plane data or backing buffer");
        const auto address = reinterpret_cast<std::uintptr_t>(frame->data[plane]);
        const auto bufferStart = reinterpret_cast<std::uintptr_t>(buffer->data);
        // Negative strides point at the final row. Palette planes have no row stride.
        const auto preceding = frame->linesize[plane] < 0 ? sizes[plane] - lines[plane] : 0;
        if (address < preceding)
            throw std::runtime_error("Decoded plane address underflow");
        const auto start = address - preceding;
        if (start < bufferStart || start - bufferStart > buffer->size ||
            sizes[plane] > buffer->size - (start - bufferStart))
            throw std::runtime_error("Decoded frame plane exceeds its backing buffer");
    }
}

void ObserveAdapter(const AVFrame* frame, DecodeObservation& observation)
{
#ifdef _WIN32
    if (!frame->hw_frames_ctx) {
        observation.adapterDiagnostic = "Hardware frame has no AVHWFramesContext";
        return;
    }
    const auto* frames = reinterpret_cast<const AVHWFramesContext*>(frame->hw_frames_ctx->data);
    const auto* device = frames->device_ctx;
    if (!device || device->type != AV_HWDEVICE_TYPE_D3D11VA || !device->hwctx) {
        observation.adapterDiagnostic = "Decoded frame does not expose a D3D11VA device";
        return;
    }
    const auto* d3d = static_cast<const AVD3D11VADeviceContext*>(device->hwctx);
    if (!d3d->device) {
        observation.adapterDiagnostic = "Decoded frame's D3D11VA device is null";
        return;
    }
    struct DxgiDeleter {
        void operator()(IDXGIDevice* value) const { value->Release(); }
    };
    struct AdapterDeleter {
        void operator()(IDXGIAdapter* value) const { value->Release(); }
    };
    IDXGIDevice* rawDevice = nullptr;
    // An inline GUID avoids an IID symbol dependency on dxgi.lib/uuid.lib.
    constexpr GUID dxgiDeviceId{0x54ec77fa, 0x1377, 0x44e6,
                               {0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c}};
    HRESULT result = d3d->device->QueryInterface(dxgiDeviceId, reinterpret_cast<void**>(&rawDevice));
    if (FAILED(result)) {
        observation.adapterDiagnostic = "D3D11 device QueryInterface(IDXGIDevice) failed: " +
                                        std::to_string(static_cast<std::uint32_t>(result));
        return;
    }
    std::unique_ptr<IDXGIDevice, DxgiDeleter> dxgiDevice(rawDevice);
    IDXGIAdapter* rawAdapter = nullptr;
    result = dxgiDevice->GetAdapter(&rawAdapter);
    if (FAILED(result)) {
        observation.adapterDiagnostic = "IDXGIDevice::GetAdapter failed: " +
                                        std::to_string(static_cast<std::uint32_t>(result));
        return;
    }
    std::unique_ptr<IDXGIAdapter, AdapterDeleter> adapter(rawAdapter);
    DXGI_ADAPTER_DESC description{};
    result = adapter->GetDesc(&description);
    if (FAILED(result)) {
        observation.adapterDiagnostic = "IDXGIAdapter::GetDesc failed: " +
                                        std::to_string(static_cast<std::uint32_t>(result));
        return;
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, description.Description,
                                         -1, nullptr, 0, nullptr, nullptr);
    if (!bytes) {
        observation.adapterDiagnostic = "DXGI adapter description could not be converted to UTF-8";
        return;
    }
    std::string utf8(static_cast<std::size_t>(bytes), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, description.Description,
                             -1, utf8.data(), bytes, nullptr, nullptr)) {
        observation.adapterDiagnostic = "DXGI adapter UTF-8 conversion failed";
        return;
    }
    utf8.pop_back();
    observation.adapter = {true, std::move(utf8), description.VendorId, description.DeviceId,
                           description.SubSysId, description.Revision,
                           description.AdapterLuid.LowPart, description.AdapterLuid.HighPart};
    observation.adapterDiagnostic = "Adapter queried from the first decoded hardware frame's D3D11 device";
#else
    (void)frame;
    observation.adapterDiagnostic = "DXGI adapter inspection is only available on Windows";
#endif
}

void DecodeFixture(DecodeObservation& observation, HardwareDecodePreference preference)
{
    // The context must outlive the codec's get_format callback and hardware frames.
    HardwareDecodeContext hardware;
    try {
        AVFormatContext* rawInput = nullptr;
        const int openResult = avformat_open_input(&rawInput, observation.media.c_str(), nullptr, nullptr);
        std::unique_ptr<AVFormatContext, FormatDeleter> input(rawInput);
        CheckAv(openResult, "Opening media");
        CheckAv(avformat_find_stream_info(input.get(), nullptr), "Reading stream information");
        const AVCodec* decoder = nullptr;
        const int streamIndex = av_find_best_stream(input.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
        CheckAv(streamIndex, "Selecting video stream");
        observation.videoStreamIndex = streamIndex;
        const AVStream* stream = input->streams[streamIndex];
        observation.codec = decoder->name;
        observation.declaredWidth = stream->codecpar->width;
        observation.declaredHeight = stream->codecpar->height;
        std::unique_ptr<AVCodecContext, CodecDeleter> codec(avcodec_alloc_context3(decoder));
        std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
        std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
        std::unique_ptr<AVFrame, FrameDeleter> transferred(av_frame_alloc());
        if (!codec || !packet || !frame || !transferred)
            throw std::runtime_error("Allocating decode resources failed");
        CheckAv(avcodec_parameters_to_context(codec.get(), stream->codecpar), "Copying video parameters");
        codec->pkt_timebase = stream->time_base;
        const HardwareDecodeResult configured = hardware.configure(codec.get(), preference);
        observation.configuredBackend = configured.backend;
        CheckAv(avcodec_open2(codec.get(), decoder, nullptr), "Opening video decoder");

        auto receive = [&]() {
            for (;;) {
                const int result = avcodec_receive_frame(codec.get(), frame.get());
                if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
                    return result;
                CheckAv(result, "Receiving decoded frame");
                ++observation.decodedFrames;
                const auto dimensions = std::pair{frame->width, frame->height};
                if (std::find(observation.dimensions.begin(), observation.dimensions.end(), dimensions) ==
                    observation.dimensions.end())
                    observation.dimensions.push_back(dimensions);
                AddFormat(observation.decodedFormats, frame->format);
                const auto* descriptor = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
                const bool hardwareFrame = descriptor && (descriptor->flags & AV_PIX_FMT_FLAG_HWACCEL);
                if ((frame->flags & AV_FRAME_FLAG_CORRUPT) || frame->decode_error_flags)
                    ++observation.corruptFrames;
                const AVFrame* software = frame.get();
                if (hardwareFrame) {
                    ++observation.hardwareFrames;
                    if (observation.hardwareFrames == 1)
                        ObserveAdapter(frame.get(), observation);
                    if (!hardware.isHardwareFrame(frame.get()))
                        throw std::runtime_error("Actual hardware pixel format is not recognized by production context");
                    software = hardware.transferToSoftware(frame.get(), transferred.get());
                    if (!software)
                        throw std::runtime_error("Production hardware-to-software frame transfer failed");
                    ++observation.transferredFrames;
                    if (software->width != frame->width || software->height != frame->height)
                        throw std::runtime_error("Transferred frame dimensions differ from hardware frame");
                } else {
                    ++observation.softwareFrames;
                }
                ValidateSoftwareFrame(software);
                AddFormat(observation.softwareFormats, software->format);
                av_frame_unref(transferred.get());
                av_frame_unref(frame.get());
            }
        };
        auto send = [&](const AVPacket* value) {
            int result = avcodec_send_packet(codec.get(), value);
            if (result == AVERROR(EAGAIN)) {
                const auto before = observation.decodedFrames;
                const int receiveResult = receive();
                if (receiveResult != AVERROR(EAGAIN) || observation.decodedFrames == before)
                    throw std::runtime_error("Decoder made no progress while retrying a packet");
                result = avcodec_send_packet(codec.get(), value);
            }
            CheckAv(result, value ? "Sending video packet" : "Flushing video decoder");
        };
        int readResult = 0;
        while ((readResult = av_read_frame(input.get(), packet.get())) >= 0) {
            if (packet->stream_index == streamIndex) {
                if (packet->flags & AV_PKT_FLAG_CORRUPT)
                    ++observation.corruptPackets;
                send(packet.get());
                if (receive() == AVERROR_EOF)
                    throw std::runtime_error("Decoder reached EOF before input drain");
            }
            av_packet_unref(packet.get());
        }
        observation.demuxEof = readResult == AVERROR_EOF;
        send(nullptr);
        observation.decoderEof = receive() == AVERROR_EOF;
        if (!observation.demuxEof)
            CheckAv(readResult, "Reading media packets");
        if (input->pb && input->pb->error < 0)
            CheckAv(input->pb->error, "Reading media I/O");
        if (!observation.decoderEof)
            throw std::runtime_error("Decoder returned EAGAIN instead of EOF after drain");
        if (!observation.decodedFrames)
            throw std::runtime_error("Video stream produced no decoded frames");
        if (observation.corruptPackets || observation.corruptFrames)
            throw std::runtime_error("Corrupt video packets or decoded frames were observed");
        if (preference == HardwareDecodePreference::Disabled && observation.hardwareFrames)
            throw std::runtime_error("Disabled hardware preference produced hardware frames");
    } catch (const std::exception& exception) {
        observation.error = exception.what();
    }
    observation.fallbackDiagnostic = hardware.result().fallbackReason;
    if (!observation.hardwareFrames && observation.fallbackDiagnostic.empty())
        observation.fallbackDiagnostic = "No hardware frames observed; decoder produced software frames";
}

void JsonString(std::ostream& output, const std::string& value)
{
    static constexpr char hex[] = "0123456789abcdef";
    output.put('"');
    for (const unsigned char character : value) {
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20)
                output << "\\u00" << hex[character >> 4] << hex[character & 15];
            else
                output.put(static_cast<char>(character));
        }
    }
    output.put('"');
}

void WriteReport(const std::string& report, const DecodeObservation& observation)
{
    std::ofstream output(std::filesystem::u8path(report), std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("Could not open JSON report: " + report);
    auto text = [&](const char* key, const std::string& value) {
        output << ",\n  \"" << key << "\": ";
        JsonString(output, value);
    };
    output << "{\n  \"schema_version\": 1";
    text("evidence_scope", "production_hardware_decode_context");
    text("linked_ffmpeg_version", av_version_info());
    text("outcome", observation.error.empty() ? "pass" : "fail");
    text("media", observation.media);
    text("requested_hardware", observation.requestedHardware);
    text("configured_backend", observation.configuredBackend);
    text("fallback_diagnostic", observation.fallbackDiagnostic);
    text("codec", observation.codec);
    text("error", observation.error);
    output << ",\n  \"hardware_active\": " << (observation.hardwareFrames > 0 ? "true" : "false")
           << ",\n  \"decoded_frames\": " << observation.decodedFrames
           << ",\n  \"hardware_frames\": " << observation.hardwareFrames
           << ",\n  \"software_frames\": " << observation.softwareFrames
           << ",\n  \"transferred_frames\": " << observation.transferredFrames
           << ",\n  \"corrupt_packets\": " << observation.corruptPackets
           << ",\n  \"corrupt_frames\": " << observation.corruptFrames
           << ",\n  \"demux_eof\": " << (observation.demuxEof ? "true" : "false")
           << ",\n  \"decoder_eof\": " << (observation.decoderEof ? "true" : "false")
           << ",\n  \"video_stream_index\": " << observation.videoStreamIndex
           << ",\n  \"declared_width\": " << observation.declaredWidth
           << ",\n  \"declared_height\": " << observation.declaredHeight
           << ",\n  \"decoded_dimensions\": [";
    bool first = true;
    for (const auto& [width, height] : observation.dimensions) {
        if (!std::exchange(first, false))
            output << ',';
        output << "{\"width\":" << width << ",\"height\":" << height << '}';
    }
    output << ']';
    auto formats = [&](const char* key, const std::vector<std::string>& values) {
        output << ",\n  \"" << key << "\": [";
        bool firstValue = true;
        for (const auto& value : values) {
            if (!std::exchange(firstValue, false))
                output << ',';
            JsonString(output, value);
        }
        output << ']';
    };
    formats("decoded_pixel_formats", observation.decodedFormats);
    formats("software_pixel_formats", observation.softwareFormats);
    text("adapter_diagnostic", observation.adapterDiagnostic);
    output << ",\n  \"adapter\": ";
    const auto& adapter = observation.adapter;
    if (!adapter.available) {
        output << "null";
    } else {
        output << "{\"description\":";
        JsonString(output, adapter.description);
        output << ",\"vendor_id\":" << adapter.vendorId << ",\"device_id\":" << adapter.deviceId
               << ",\"subsystem_id\":" << adapter.subsystemId << ",\"revision\":" << adapter.revision
               << ",\"luid_low\":" << adapter.luidLow << ",\"luid_high\":" << adapter.luidHigh << '}';
    }
    output << "\n}\n";
    output.flush();
    if (!output)
        throw std::runtime_error("Writing JSON report failed: " + report);
    output.close();
    if (!output)
        throw std::runtime_error("Closing JSON report failed: " + report);
}

std::string ArgumentUtf8(const char* value)
{
#ifdef _WIN32
    // Narrow Windows CRT arguments use the active ANSI code page, not necessarily UTF-8.
    const int wideSize = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
    if (!wideSize)
        throw std::runtime_error("Command-line argument cannot be converted from the Windows code page");
    std::wstring wide(static_cast<std::size_t>(wideSize), L'\0');
    if (!MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, value, -1, wide.data(), wideSize))
        throw std::runtime_error("Command-line argument conversion failed");
    const int utf8Size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.c_str(), -1,
                                            nullptr, 0, nullptr, nullptr);
    if (!utf8Size)
        throw std::runtime_error("Command-line argument cannot be converted to UTF-8");
    std::string utf8(static_cast<std::size_t>(utf8Size), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.c_str(), -1,
                             utf8.data(), utf8Size, nullptr, nullptr))
        throw std::runtime_error("Command-line UTF-8 conversion failed");
    utf8.pop_back();
    return utf8;
#else
    return value;
#endif
}

int RunFixture(int argc, char** argv)
{
    DecodeObservation observation;
    std::string report;
    bool hardwareSpecified = false;
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 >= argc) {
            std::cerr << "Missing option value\n";
            return 2;
        }
        const std::string option = argv[index];
        std::string value;
        try {
            value = ArgumentUtf8(argv[index + 1]);
        } catch (const std::exception& exception) {
            std::cerr << exception.what() << '\n';
            return 2;
        }
        if (value.empty()) {
            std::cerr << "Option values must not be empty\n";
            return 2;
        }
        if (option == "--media" && observation.media.empty())
            observation.media = value;
        else if (option == "--hardware" && !hardwareSpecified) {
            observation.requestedHardware = value;
            hardwareSpecified = true;
        } else if (option == "--report" && report.empty())
            report = value;
        else {
            std::cerr << "Unknown or duplicate option: " << option << '\n';
            return 2;
        }
    }
    HardwareDecodePreference preference = HardwareDecodePreference::Disabled;
    if (observation.requestedHardware == "auto")
        preference = HardwareDecodePreference::Auto;
    else if (observation.requestedHardware == "d3d11va")
        preference = HardwareDecodePreference::D3D11VA;
    else if (observation.requestedHardware != "disabled")
        hardwareSpecified = false;
    if (observation.media.empty() || report.empty() || !hardwareSpecified) {
        std::cerr << "Usage: hardware_decode_policy_tests --media <path> "
                     "--hardware disabled|auto|d3d11va --report <path>\n";
        return 2;
    }
    DecodeFixture(observation, preference);
    try {
        WriteReport(report, observation);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    if (!observation.error.empty())
        std::cerr << observation.error << '\n';
    return observation.error.empty() ? 0 : 1;
}
}

int main(int argc, char** argv)
{
    if (argc > 1)
        return RunFixture(argc, argv);

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
    const AVPixelFormat softwareFormats[] = {AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE};
    if (!Expect(!disabled.active,
                "disabled preference must not activate hardware decoding") ||
        !Expect(codecContext->hw_device_ctx == nullptr && codecContext->opaque == nullptr &&
                    codecContext->get_format == avcodec_default_get_format &&
                    codecContext->get_format(codecContext, softwareFormats) == AV_PIX_FMT_YUV420P,
                "disabled preference restores usable default software pixel-format selection")) {
        avcodec_free_context(&codecContext);
        return 1;
    }
    avcodec_free_context(&codecContext);
    return 0;
}
