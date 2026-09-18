#include "app_preferences.h"

#include <QDir>
#include <QTemporaryDir>

#include <iostream>

namespace {

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

bool PreferencesEqual(const AppPreferences& left, const AppPreferences& right)
{
    return left.volume == right.volume
        && left.speed == right.speed
        && left.loopPolicy == right.loopPolicy
        && left.resumePlayback == right.resumePlayback
        && left.reconnectAttempts == right.reconnectAttempts
        && left.connectTimeoutMs == right.connectTimeoutMs
        && left.readTimeoutMs == right.readTimeoutMs
        && left.rtspTransport == right.rtspTransport;
}

} // namespace

int main()
{
    AppPreferences invalid;
    invalid.volume = 2.0;
    invalid.speed = 3.0;
    invalid.loopPolicy = static_cast<VideoLoopPolicy>(999);
    invalid.reconnectAttempts = -1;
    invalid.connectTimeoutMs = 50;
    invalid.readTimeoutMs = 999999;
    invalid.rtspTransport = static_cast<RtspTransport>(99);

    const AppPreferences sanitized = SanitizePreferences(invalid);
    if (!Expect(sanitized.volume == 1.0, "volume above one clamps to one"))
        return 1;
    if (!Expect(sanitized.speed == 2.0, "speed above supported range clamps to two times"))
        return 1;
    if (!Expect(sanitized.loopPolicy == VideoLoopPolicy::LOOP_ALL,
                "invalid loop policy falls back to list looping"))
        return 1;
    if (!Expect(sanitized.reconnectAttempts == 0, "negative reconnect attempts clamp to zero"))
        return 1;
    if (!Expect(sanitized.connectTimeoutMs == 1000, "connect timeout has a one second minimum"))
        return 1;
    if (!Expect(sanitized.readTimeoutMs == 120000, "read timeout has a two minute maximum"))
        return 1;
    if (!Expect(sanitized.rtspTransport == RtspTransport::Tcp,
                "invalid RTSP transport falls back to TCP"))
        return 1;

    AppPreferences low;
    low.volume = -0.5;
    low.speed = 0.01;
    low.connectTimeoutMs = 2000;
    low.readTimeoutMs = 3000;
    const AppPreferences lowSanitized = SanitizePreferences(low);
    if (!Expect(lowSanitized.volume == 0.0, "negative volume clamps to zero"))
        return 1;
    if (!Expect(lowSanitized.speed == 0.1, "speed below supported range clamps to one tenth"))
        return 1;

    QTemporaryDir directory;
    if (!Expect(directory.isValid(), "temporary preferences directory is available"))
        return 1;

    AppPreferences persisted;
    persisted.volume = 0.3;
    persisted.speed = 1.5;
    persisted.loopPolicy = VideoLoopPolicy::LOOP_RANDOM;
    persisted.resumePlayback = false;
    persisted.reconnectAttempts = 8;
    persisted.connectTimeoutMs = 12000;
    persisted.readTimeoutMs = 24000;
    persisted.rtspTransport = RtspTransport::Udp;
    const QString path = QDir(directory.path()).filePath("preferences.ini");

    SavePreferences(path, persisted);
    if (!Expect(PreferencesEqual(LoadPreferences(path), persisted),
                "preferences round trip through the supplied INI path"))
        return 1;

    return 0;
}
