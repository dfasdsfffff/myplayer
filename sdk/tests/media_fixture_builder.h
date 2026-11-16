#pragma once

#include <filesystem>

struct GeneratedMediaFixtures {
    std::filesystem::path wav;
    std::filesystem::path video;
    std::filesystem::path subtitle;
};

GeneratedMediaFixtures BuildMediaFixtures(const std::filesystem::path& directory, int videoFrameCount = 10);
// MPEG-4 B-frames, alternating 40/120 ms intervals, distinct luma, and PCM audio.
std::filesystem::path BuildFrameStepFixture(const std::filesystem::path& directory);
