#include "media_fixture_builder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
}

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <numbers>
#include <stdexcept>

namespace {

void WriteLe16(std::ofstream& output, std::uint16_t value)
{
    output.put(static_cast<char>(value & 0xff));
    output.put(static_cast<char>(value >> 8));
}

void WriteLe32(std::ofstream& output, std::uint32_t value)
{
    WriteLe16(output, static_cast<std::uint16_t>(value & 0xffff));
    WriteLe16(output, static_cast<std::uint16_t>(value >> 16));
}

void WriteWav(const std::filesystem::path& path)
{
    constexpr int sampleRate = 8000;
    constexpr int samples = sampleRate / 2;
    std::array<std::int16_t, samples> pcm{};
    for (int index = 0; index < samples; ++index)
        pcm[index] = static_cast<std::int16_t>(std::sin(index * 2.0 * std::numbers::pi * 440.0 / sampleRate) * 10000.0);

    std::ofstream output(path, std::ios::binary);
    if (!output)
        throw std::runtime_error("cannot create WAV fixture");
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(pcm.size() * sizeof(std::int16_t));
    output.write("RIFF", 4);
    WriteLe32(output, 36 + dataBytes);
    output.write("WAVEfmt ", 8);
    WriteLe32(output, 16);
    WriteLe16(output, 1);
    WriteLe16(output, 1);
    WriteLe32(output, sampleRate);
    WriteLe32(output, sampleRate * sizeof(std::int16_t));
    WriteLe16(output, sizeof(std::int16_t));
    WriteLe16(output, 16);
    output.write("data", 4);
    WriteLe32(output, dataBytes);
    output.write(reinterpret_cast<const char*>(pcm.data()), dataBytes);
}

void WriteVideo(const std::filesystem::path& path)
{
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    try {
        if (avformat_alloc_output_context2(&format, nullptr, "avi", path.string().c_str()) < 0 || !format)
            throw std::runtime_error("cannot allocate video fixture output");
        const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
        if (!encoder)
            throw std::runtime_error("MPEG-4 encoder unavailable");
        AVStream* stream = avformat_new_stream(format, encoder);
        codec = avcodec_alloc_context3(encoder);
        if (!stream || !codec)
            throw std::runtime_error("cannot allocate video fixture stream");
        codec->width = 64;
        codec->height = 48;
        codec->pix_fmt = AV_PIX_FMT_YUV420P;
        codec->time_base = AVRational{1, 10};
        codec->framerate = AVRational{10, 1};
        codec->gop_size = 10;
        if (format->oformat->flags & AVFMT_GLOBALHEADER)
            codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(codec, encoder, nullptr) < 0 ||
            avcodec_parameters_from_context(stream->codecpar, codec) < 0)
            throw std::runtime_error("cannot open video fixture encoder");
        stream->time_base = codec->time_base;
        if (!(format->oformat->flags & AVFMT_NOFILE) && avio_open(&format->pb, path.string().c_str(), AVIO_FLAG_WRITE) < 0)
            throw std::runtime_error("cannot open video fixture");
        if (avformat_write_header(format, nullptr) < 0)
            throw std::runtime_error("cannot write video fixture header");
        frame = av_frame_alloc();
        packet = av_packet_alloc();
        if (!frame || !packet)
            throw std::runtime_error("cannot allocate video fixture frame");
        frame->format = codec->pix_fmt;
        frame->width = codec->width;
        frame->height = codec->height;
        if (av_frame_get_buffer(frame, 32) < 0)
            throw std::runtime_error("cannot allocate video fixture pixels");
        for (int index = 0; index < 10; ++index) {
            av_frame_make_writable(frame);
            for (int y = 0; y < frame->height; ++y)
                std::fill_n(frame->data[0] + y * frame->linesize[0], frame->width, index < 5 ? 64 : 192);
            for (int y = 0; y < frame->height / 2; ++y) {
                std::fill_n(frame->data[1] + y * frame->linesize[1], frame->width / 2, 90);
                std::fill_n(frame->data[2] + y * frame->linesize[2], frame->width / 2, 240);
            }
            frame->pts = index;
            if (avcodec_send_frame(codec, frame) < 0)
                throw std::runtime_error("cannot encode video fixture frame");
            while (avcodec_receive_packet(codec, packet) == 0) {
                av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
                packet->stream_index = stream->index;
                if (av_interleaved_write_frame(format, packet) < 0)
                    throw std::runtime_error("cannot write video fixture packet");
                av_packet_unref(packet);
            }
        }
        avcodec_send_frame(codec, nullptr);
        while (avcodec_receive_packet(codec, packet) == 0) {
            av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
            packet->stream_index = stream->index;
            if (av_interleaved_write_frame(format, packet) < 0)
                throw std::runtime_error("cannot flush video fixture packet");
            av_packet_unref(packet);
        }
        av_write_trailer(format);
    } catch (...) {
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&codec);
        if (format && !(format->oformat->flags & AVFMT_NOFILE) && format->pb)
            avio_closep(&format->pb);
        avformat_free_context(format);
        throw;
    }
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codec);
    if (!(format->oformat->flags & AVFMT_NOFILE))
        avio_closep(&format->pb);
    avformat_free_context(format);
}

} // namespace

GeneratedMediaFixtures BuildMediaFixtures(const std::filesystem::path& directory)
{
    std::filesystem::create_directories(directory);
    GeneratedMediaFixtures fixtures{directory / "tone.wav", directory / "colors.avi", directory / "caption.srt"};
    WriteWav(fixtures.wav);
    WriteVideo(fixtures.video);
    std::ofstream subtitle(fixtures.subtitle);
    if (!subtitle)
        throw std::runtime_error("cannot create subtitle fixture");
    subtitle << "1\n00:00:00,000 --> 00:00:00,800\nDeterministic caption\n";
    return fixtures;
}
