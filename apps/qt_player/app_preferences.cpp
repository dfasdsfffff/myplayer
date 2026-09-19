#include "app_preferences.h"

#include <QSettings>

#include <algorithm>

namespace {

constexpr double kMinimumSpeed = 0.1;
constexpr double kMaximumSpeed = 2.0;
constexpr int kMinimumTimeoutMs = 1000;
constexpr int kMaximumTimeoutMs = 120000;

bool IsValidLoopPolicy(VideoLoopPolicy policy)
{
    return policy >= VideoLoopPolicy::LOOP_NONE && policy < VideoLoopPolicy::LOOP_MAX;
}

bool IsValidRtspTransport(RtspTransport transport)
{
    return transport == RtspTransport::Tcp || transport == RtspTransport::Udp;
}

bool IsValidHardwareDecodePreference(HardwareDecodePreference preference)
{
    return preference == HardwareDecodePreference::Auto ||
        preference == HardwareDecodePreference::Disabled ||
        preference == HardwareDecodePreference::D3D11VA;
}

} // namespace

AppPreferences SanitizePreferences(AppPreferences value)
{
    value.volume = std::clamp(value.volume, 0.0, 1.0);
    value.speed = std::clamp(value.speed, kMinimumSpeed, kMaximumSpeed);
    if (!IsValidLoopPolicy(value.loopPolicy))
        value.loopPolicy = VideoLoopPolicy::LOOP_ALL;
    value.reconnectAttempts = std::max(value.reconnectAttempts, 0);
    value.connectTimeoutMs = std::clamp(value.connectTimeoutMs, kMinimumTimeoutMs, kMaximumTimeoutMs);
    value.readTimeoutMs = std::clamp(value.readTimeoutMs, kMinimumTimeoutMs, kMaximumTimeoutMs);
    if (!IsValidRtspTransport(value.rtspTransport))
        value.rtspTransport = RtspTransport::Tcp;
	if (!IsValidHardwareDecodePreference(value.hardwareDecode))
		value.hardwareDecode = HardwareDecodePreference::Auto;
    return value;
}

AppPreferences LoadPreferences(const QString& path)
{
    QSettings settings(path, QSettings::IniFormat);
    AppPreferences preferences;
    settings.beginGroup("preferences");
    preferences.volume = settings.value("volume", preferences.volume).toDouble();
    preferences.speed = settings.value("speed", preferences.speed).toDouble();
    preferences.loopPolicy = static_cast<VideoLoopPolicy>(settings.value("loopPolicy", static_cast<int>(preferences.loopPolicy)).toInt());
    preferences.resumePlayback = settings.value("resumePlayback", preferences.resumePlayback).toBool();
    preferences.reconnectAttempts = settings.value("reconnectAttempts", preferences.reconnectAttempts).toInt();
    preferences.connectTimeoutMs = settings.value("connectTimeoutMs", preferences.connectTimeoutMs).toInt();
    preferences.readTimeoutMs = settings.value("readTimeoutMs", preferences.readTimeoutMs).toInt();
    preferences.rtspTransport = static_cast<RtspTransport>(settings.value("rtspTransport", static_cast<int>(preferences.rtspTransport)).toInt());
	preferences.hardwareDecode = static_cast<HardwareDecodePreference>(settings.value("hardwareDecode", static_cast<int>(preferences.hardwareDecode)).toInt());
    settings.endGroup();
    return SanitizePreferences(preferences);
}

void SavePreferences(const QString& path, const AppPreferences& preferences)
{
    const AppPreferences sanitized = SanitizePreferences(preferences);
    QSettings settings(path, QSettings::IniFormat);
    settings.beginGroup("preferences");
    settings.setValue("volume", sanitized.volume);
    settings.setValue("speed", sanitized.speed);
    settings.setValue("loopPolicy", static_cast<int>(sanitized.loopPolicy));
    settings.setValue("resumePlayback", sanitized.resumePlayback);
    settings.setValue("reconnectAttempts", sanitized.reconnectAttempts);
    settings.setValue("connectTimeoutMs", sanitized.connectTimeoutMs);
    settings.setValue("readTimeoutMs", sanitized.readTimeoutMs);
    settings.setValue("rtspTransport", static_cast<int>(sanitized.rtspTransport));
	settings.setValue("hardwareDecode", static_cast<int>(sanitized.hardwareDecode));
    settings.endGroup();
    settings.sync();
}
