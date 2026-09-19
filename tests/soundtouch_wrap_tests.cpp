#include "soundtouch_wrap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

int main()
{
    constexpr int sampleRate = 48000;
    constexpr int channels = 2;
    constexpr int inputFrames = sampleRate;
    std::vector<short> input(inputFrames * channels);
    for (int frame = 0; frame < inputFrames; ++frame) {
        const auto sample = static_cast<short>(std::sin(frame * 0.01) * 12000.0);
        input[frame * channels] = sample;
        input[frame * channels + 1] = sample;
    }

    constexpr size_t outputSamples = inputFrames * channels * 3;
    std::vector<short> output(outputSamples + 2, static_cast<short>(0x5a5a));
    void* handle = soundtouch_create();
    if (!Expect(handle != nullptr, "SoundTouch instance should be created"))
        return 1;

    const int producedBytes = soundtouch_translate(handle, input.data(), 0.5f,
        static_cast<int>(input.size()), sizeof(short), channels, sampleRate,
        output.data(), outputSamples);
    soundtouch_destroy(handle);

    if (!Expect(producedBytes > 0, "slow playback should produce PCM") ||
        !Expect(producedBytes > static_cast<int>(input.size() * sizeof(short)), "0.5x tempo should expand PCM duration") ||
        !Expect(static_cast<size_t>(producedBytes) <= outputSamples * sizeof(short), "PCM output must fit its supplied capacity") ||
        !Expect(output[outputSamples] == static_cast<short>(0x5a5a) && output[outputSamples + 1] == static_cast<short>(0x5a5a),
            "SoundTouch must not write past the supplied PCM output buffer"))
        return 1;
    return 0;
}
