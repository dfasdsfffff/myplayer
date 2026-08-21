#pragma once

#include <algorithm>
#include <cmath>

namespace PlaybackSettings {

inline double NormalizeVolume(double volume)
{
    return std::clamp(volume, 0.0, 1.0);
}

inline int ToSdlVolume(double normalizedVolume, int maximumVolume)
{
    return static_cast<int>(std::lround(NormalizeVolume(normalizedVolume) * maximumVolume));
}

} // namespace PlaybackSettings
