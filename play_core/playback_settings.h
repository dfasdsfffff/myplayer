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

inline bool ShouldResume(int savedSeconds, int durationSeconds, bool seekable)
{
	return seekable && durationSeconds > 0 && savedSeconds > 5 && savedSeconds < durationSeconds;
}

inline bool ShouldClearResume(int currentSeconds, int durationSeconds, bool completed)
{
	if (completed)
		return true;
	if (durationSeconds <= 0 || currentSeconds < 0)
		return false;
	return durationSeconds - currentSeconds <= 30 || currentSeconds * 100 >= durationSeconds * 95;
}
