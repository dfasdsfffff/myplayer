#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

enum class TrackKind { Audio, Subtitle };

struct SeekCommand {
    int64_t position = 0;
    int64_t relative = 0;
    int flags = 0;
};

struct TrackCommand {
    TrackKind kind = TrackKind::Audio;
    std::optional<int> streamIndex;
    bool cycle = false;
};

struct PlaybackCommands {
    unsigned pauseToggleCount = 0;
    std::optional<SeekCommand> seek;
    std::vector<TrackCommand> tracks;
};

class PlaybackCommandMailbox final {
public:
    void postPauseToggle();
    void postSeek(SeekCommand command);
    void postTrack(TrackCommand command);
    PlaybackCommands take();
    void clear();

private:
    std::mutex m_mutex;
    PlaybackCommands m_pending;
};
