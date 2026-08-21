#include "playback_settings.h"

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
    if (!Expect(PlaybackSettings::NormalizeVolume(-0.1) == 0.0, "negative volume clamps to zero"))
        return 1;
    if (!Expect(PlaybackSettings::NormalizeVolume(1.1) == 1.0, "volume above one clamps to one"))
        return 1;
    if (!Expect(PlaybackSettings::ToSdlVolume(0.30, 128) == 38, "30 percent converts to SDL volume once"))
        return 1;
    if (!Expect(PlaybackSettings::ToSdlVolume(0.30, 128) == 38, "repeated conversion does not drift"))
        return 1;
    if (!Expect(PlaybackSettings::ToSdlVolume(1.0, 128) == 128, "maximum volume converts exactly"))
        return 1;

    return 0;
}
