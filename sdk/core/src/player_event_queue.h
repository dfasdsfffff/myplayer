#pragma once

#include <play_core/player.h>
#include <algorithm>
#include <deque>
#include <utility>

namespace play_core::detail {
inline void discardInactiveEvents(std::deque<PlayerEvent>& events, SessionId activeSession) {
    std::erase_if(events, [activeSession](const PlayerEvent& event) {
        return event.type != PlayerEvent::Type::Operation && event.session != activeSession;
    });
}

inline bool enqueuePlayerEvent(std::deque<PlayerEvent>& events, PlayerEvent event, SessionId activeSession, bool loop = false) {
    using Type = PlayerEvent::Type;
    if (event.type != Type::Operation && event.session != activeSession) return false;
    if (event.type == Type::Video || event.type == Type::Subtitle || event.type == Type::Position ||
        event.type == Type::MediaInfo || (loop && event.type == Type::Ended)) {
        auto existing = std::find_if(events.begin(), events.end(), [&](const PlayerEvent& queued) {
            return queued.type == event.type && queued.session == event.session;
        });
        if (existing != events.end()) { *existing = std::move(event); return true; }
    }
    events.push_back(std::move(event));
    return true;
}
}
