#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SubtitleBitmap {
    int x{0};
    int y{0};
    int width{0};
    int height{0};
    int stride{0};
    std::vector<std::uint8_t> bgra;
};

struct SubtitleFrame {
    double startSeconds{0.0};
    double endSeconds{0.0};
    std::string assOrText;
    std::vector<SubtitleBitmap> bitmaps;
};
