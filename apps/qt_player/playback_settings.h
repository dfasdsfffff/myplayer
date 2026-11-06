#pragma once
#include <play_core/player.h>
#include <algorithm>
#include <cmath>
using HardwareDecodePreference = play_core::HardwareDecode;
namespace PlaybackSettings {
inline double NormalizeVolume(double v) { return std::clamp(v,0.,1.); }
inline int ToSdlVolume(double v,int maximum) { return static_cast<int>(std::lround(NormalizeVolume(v)*maximum)); }
}
inline bool ShouldResume(int saved,int duration,bool seekable) { return seekable && duration > 0 && saved > 5 && saved < duration; }
inline bool ShouldClearResume(int current,int duration,bool completed) {
    return completed || (duration > 0 && current >= 0 && (duration-current <= 30 || static_cast<int64_t>(current)*100 >= static_cast<int64_t>(duration)*95));
}
