#include "playback_command_mailbox.h"

#include <atomic>
#include <iostream>
#include <thread>

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
    PlaybackCommandMailbox mailbox;

    mailbox.postPauseToggle();
    mailbox.postPauseToggle();
    mailbox.postSeek({100, 0, 0});
    mailbox.postSeek({200, 5, 1});
    mailbox.postTrack({TrackKind::Audio, 3, false});
    mailbox.postTrack({TrackKind::Subtitle, std::nullopt, true});

    const PlaybackCommands first = mailbox.take();
    if (!Expect(first.pauseToggleCount == 2, "pause toggles preserve parity count") ||
        !Expect(first.seek && first.seek->position == 200 && first.seek->relative == 5 && first.seek->flags == 1,
            "latest seek replaces older seek") ||
        !Expect(first.tracks.size() == 2 && first.tracks[0].kind == TrackKind::Audio &&
            first.tracks[0].streamIndex == 3 && first.tracks[1].cycle,
            "track commands preserve order"))
        return 1;

    const PlaybackCommands empty = mailbox.take();
    if (!Expect(empty.pauseToggleCount == 0 && !empty.seek && empty.tracks.empty(),
            "take clears consumed commands"))
        return 1;

    std::atomic_bool start{false};
    std::vector<std::thread> producers;
    for (int producer = 0; producer < 4; ++producer) {
        producers.emplace_back([&mailbox, &start] {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            for (int i = 0; i < 500; ++i)
                mailbox.postPauseToggle();
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& producer : producers)
        producer.join();
    const PlaybackCommands concurrent = mailbox.take();
    if (!Expect(concurrent.pauseToggleCount == 2000, "concurrent producers do not lose commands"))
        return 1;

    mailbox.postSeek({300, 0, 0});
    mailbox.postPauseToggle();
    mailbox.clear();
    const PlaybackCommands cleared = mailbox.take();
    return Expect(cleared.pauseToggleCount == 0 && !cleared.seek && cleared.tracks.empty(),
        "clear drops commands during stop") ? 0 : 1;
}
