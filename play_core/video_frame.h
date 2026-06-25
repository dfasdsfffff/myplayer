#pragma once

#include <cstdint>
#include <vector>

struct VideoFrame
{
	int width = 0;
	int height = 0;
	int bytesPerLine = 0;
	std::vector<uint8_t> bgra;
};
