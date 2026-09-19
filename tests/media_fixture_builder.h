#pragma once

#include <filesystem>

struct GeneratedMediaFixtures {
    std::filesystem::path wav;
    std::filesystem::path video;
    std::filesystem::path subtitle;
};

GeneratedMediaFixtures BuildMediaFixtures(const std::filesystem::path& directory);
