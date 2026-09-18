#define SDL_MAIN_HANDLED

#include "video_frame.h"

#include <iostream>

namespace {

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

} // namespace

int main()
{
    const VideoDisplayRect square = ComputeVideoDisplayRect(1920, 1080, {1, 1}, 0.0, 1280, 720);
    if (!Expect(square.x == 0 && square.y == 0 && square.width == 1280 && square.height == 720,
            "square pixels should fill an equally shaped viewport"))
        return 1;

    const VideoDisplayRect anamorphic = ComputeVideoDisplayRect(720, 576, {16, 15}, 0.0, 1280, 720);
    if (!Expect(anamorphic.x == 160 && anamorphic.y == 0 && anamorphic.width == 960 && anamorphic.height == 720,
            "anamorphic SAR should widen the display rectangle before fitting"))
        return 1;

    const VideoDisplayRect rotated90 = ComputeVideoDisplayRect(1920, 1080, {1, 1}, 90.0, 1280, 720);
    if (!Expect(rotated90.x == 437 && rotated90.y == 0 && rotated90.width == 405 && rotated90.height == 720,
            "quarter-turn rotation should swap display dimensions"))
        return 1;

    const VideoDisplayRect rotated180 = ComputeVideoDisplayRect(1920, 1080, {1, 1}, 180.0, 1280, 720);
    if (!Expect(rotated180.width == 1280 && rotated180.height == 720,
            "half-turn rotation should preserve display dimensions"))
        return 1;

    const VideoDisplayRect invalidSar = ComputeVideoDisplayRect(1920, 1080, {0, 0}, 270.0, 1280, 720);
    if (!Expect(invalidSar.width == 405 && invalidSar.height == 720,
            "invalid SAR should fall back to square pixels"))
        return 1;

    return 0;
}
