#pragma once
#include <play_core/media_source.h>
#include <array>
#include <memory>
#include <vector>
namespace play_core {
enum class VideoFrameFormat { Bgra32, Yuv420P };
enum class ColorSpace { Unknown, Bt601, Bt709, Bt2020 };
enum class ColorRange { Unknown, Limited, Full };
struct VideoPlane { std::shared_ptr<const std::uint8_t> data; int stride{0}; int height{0}; };
// Shared immutable CPU storage. Views remain valid while the frame is retained,
// including after close()/Player destruction. Unknown PTS is nullopt.
struct VideoFrame {
    VideoFrameFormat format{VideoFrameFormat::Bgra32};
    int width{0}, height{0}, bytesPerLine{0};
    Rational sampleAspectRatio;
    double rotationDegrees{0};
    ColorSpace colorSpace{ColorSpace::Unknown};
    ColorRange colorRange{ColorRange::Unknown};
    bool unsupportedHdrTransfer{false};
    std::optional<std::int64_t> timestampMs;
    std::shared_ptr<const std::vector<std::uint8_t>> bgra;
    std::array<VideoPlane, 3> planes;
};
struct SubtitleBitmap { int x{0}, y{0}, width{0}, height{0}, stride{0}; std::vector<std::uint8_t> bgra; };
struct SubtitleFrame {
    std::int64_t startMs{0}, endMs{0};
    std::string assOrText;
    std::string assHeader; // codec ASS style/header data for packet subtitles
    bool assPacket{false}; // assOrText is an ASS packet, not a complete script
    std::vector<SubtitleBitmap> bitmaps;
};
}
