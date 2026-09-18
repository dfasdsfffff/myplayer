#pragma once

#include <QString>

#include "media_source.h"

enum class StatusPresentationKind {
    Transient,
    Persistent,
    FinalError,
};

struct StatusPresentation {
    QString text;
    StatusPresentationKind kind{StatusPresentationKind::Transient};
};

StatusPresentation PresentPlaybackStatus(const PlaybackStatus& status);
