#pragma once

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
}

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

enum class VideoFrameFormat { Bgra32, Yuv420P };

struct VideoPlane {
	std::shared_ptr<const uint8_t> data;
	int stride = 0;
	int height = 0;
};

struct VideoDisplayRect {
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
};

inline VideoDisplayRect ComputeVideoDisplayRect(int codedWidth, int codedHeight, AVRational sampleAspectRatio,
	 double rotationDegrees, int viewportWidth, int viewportHeight)
{
	if (codedWidth <= 0 || codedHeight <= 0 || viewportWidth <= 0 || viewportHeight <= 0)
		return {};

	double sar = 1.0;
	if (sampleAspectRatio.num > 0 && sampleAspectRatio.den > 0)
		sar = static_cast<double>(sampleAspectRatio.num) / sampleAspectRatio.den;
	double displayWidth = codedWidth * sar;
	double displayHeight = codedHeight;
	double normalizedRotation = std::fmod(std::abs(rotationDegrees), 360.0);
	if (std::abs(normalizedRotation - 90.0) < 0.01 || std::abs(normalizedRotation - 270.0) < 0.01)
		std::swap(displayWidth, displayHeight);

	const double aspect = displayWidth / displayHeight;
	int width = viewportWidth;
	int height = static_cast<int>(std::lround(width / aspect));
	if (height > viewportHeight) {
		height = viewportHeight;
		width = static_cast<int>(std::lround(height * aspect));
	}
	return {(viewportWidth - width) / 2, (viewportHeight - height) / 2, width, height};
}

struct VideoFrame
{
	VideoFrameFormat format = VideoFrameFormat::Bgra32;
	int width = 0;
	int height = 0;
	int bytesPerLine = 0;
	AVRational sampleAspectRatio{1, 1};
	double rotationDegrees = 0.0;
	AVColorSpace colorSpace = AVCOL_SPC_UNSPECIFIED;
	AVColorRange colorRange = AVCOL_RANGE_UNSPECIFIED;
	bool unsupportedHdrTransfer = false;
	std::vector<uint8_t> bgra;
	std::array<VideoPlane, 3> planes;
};
