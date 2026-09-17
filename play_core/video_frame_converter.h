#pragma once

#include "av_compat.h"
#include "video_frame.h"

#include <array>
#include <cstddef>
#include <memory>

class VideoFrameConverter {
public:
	VideoFrameConverter();
	~VideoFrameConverter();

	VideoFrameConverter(const VideoFrameConverter&) = delete;
	VideoFrameConverter& operator=(const VideoFrameConverter&) = delete;

	std::shared_ptr<VideoFrame> convert(const AVFrame* source);

private:
	std::shared_ptr<VideoFrame> acquireFrame();

	SwsContext* m_context = nullptr;
	std::array<std::shared_ptr<VideoFrame>, 3> m_frames;
	std::size_t m_nextFrame = 0;
};
