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
    if (!Expect(ShouldResume(10, 100, true), "resume requires more than five seconds on a seekable source") ||
        !Expect(!ShouldResume(5, 100, true), "five seconds is not enough to resume") ||
        !Expect(!ShouldResume(10, 100, false), "live or non-seekable sources must not resume") ||
        !Expect(!ShouldResume(10, 0, true), "unknown duration must not resume"))
        return 1;

    if (!Expect(ShouldClearResume(70, 100, false), "resume clears inside the final thirty seconds") ||
        !Expect(ShouldClearResume(95, 100, false), "resume clears at ninety-five percent") ||
        !Expect(ShouldClearResume(50, 100, true), "completed playback clears resume") ||
        !Expect(!ShouldClearResume(50, 100, false), "mid-playback resume remains available"))
        return 1;

    return 0;
}
