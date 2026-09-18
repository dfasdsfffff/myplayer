#include "video_frame_converter.h"

VideoFrameConverter::VideoFrameConverter()
{
	for (auto& frame : m_frames)
		frame = std::make_shared<VideoFrame>();
}

VideoFrameConverter::~VideoFrameConverter()
{
	sws_freeContext(m_context);
}

std::shared_ptr<VideoFrame> VideoFrameConverter::convert(const AVFrame* source)
{
	if (!source || source->width <= 0 || source->height <= 0 || source->format < 0)
		return nullptr;

	auto frame = acquireFrame();
	frame->width = source->width;
	frame->height = source->height;
	frame->bytesPerLine = frame->width * 4;
	frame->sampleAspectRatio = (source->sample_aspect_ratio.num > 0 && source->sample_aspect_ratio.den > 0)
		? source->sample_aspect_ratio : AVRational{1, 1};
	frame->colorSpace = source->colorspace;
	frame->colorRange = source->color_range;
	frame->unsupportedHdrTransfer = source->color_trc == AVCOL_TRC_SMPTE2084
		|| source->color_trc == AVCOL_TRC_ARIB_STD_B67;
	frame->rotationDegrees = 0.0;
	if (const AVFrameSideData* displayMatrix = av_frame_get_side_data(source, AV_FRAME_DATA_DISPLAYMATRIX))
		frame->rotationDegrees = -av_display_rotation_get(reinterpret_cast<const int32_t*>(displayMatrix->data));
	frame->bgra.resize(static_cast<std::size_t>(frame->bytesPerLine) * frame->height);

	m_context = sws_getCachedContext(m_context,
		source->width, source->height, static_cast<AVPixelFormat>(source->format),
		source->width, source->height, AV_PIX_FMT_BGRA,
		SWS_BICUBIC, nullptr, nullptr, nullptr);
	if (!m_context)
		return nullptr;
	const int* coefficients = sws_getCoefficients(SWS_CS_DEFAULT);
	if (source->colorspace == AVCOL_SPC_BT709)
		coefficients = sws_getCoefficients(SWS_CS_ITU709);
	else if (source->colorspace == AVCOL_SPC_SMPTE170M || source->colorspace == AVCOL_SPC_SMPTE240M)
		coefficients = sws_getCoefficients(SWS_CS_SMPTE170M);
	const int sourceRange = source->color_range == AVCOL_RANGE_JPEG;
	if (sws_setColorspaceDetails(m_context, coefficients, sourceRange, coefficients, 0, 0, 1 << 16, 1 << 16) < 0)
		return nullptr;

	uint8_t* destinationData[4] = { frame->bgra.data(), nullptr, nullptr, nullptr };
	int destinationLinesize[4] = { frame->bytesPerLine, 0, 0, 0 };
	const int convertedRows = sws_scale(m_context,
		reinterpret_cast<const uint8_t* const*>(source->data), source->linesize,
		0, source->height, destinationData, destinationLinesize);
	if (convertedRows != source->height)
		return nullptr;

	return frame;
}

std::shared_ptr<VideoFrame> VideoFrameConverter::acquireFrame()
{
	for (std::size_t offset = 0; offset < m_frames.size(); ++offset) {
		const auto slot = (m_nextFrame + offset) % m_frames.size();
		if (m_frames[slot].use_count() == 1) {
			m_nextFrame = (slot + 1) % m_frames.size();
			return m_frames[slot];
		}
	}

	return std::make_shared<VideoFrame>();
}
