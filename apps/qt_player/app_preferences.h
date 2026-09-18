#pragma once

#include "enums.h"
#include "media_source.h"

#include <QMetaType>
#include <QString>

struct AppPreferences {
    double volume{1.0};
    double speed{1.0};
    VideoLoopPolicy loopPolicy{VideoLoopPolicy::LOOP_ALL};
    bool resumePlayback{true};
    int reconnectAttempts{5};
    int connectTimeoutMs{10000};
    int readTimeoutMs{15000};
    RtspTransport rtspTransport{RtspTransport::Tcp};
};

AppPreferences SanitizePreferences(AppPreferences value);
AppPreferences LoadPreferences(const QString& path);
void SavePreferences(const QString& path, const AppPreferences& preferences);

Q_DECLARE_METATYPE(AppPreferences)
