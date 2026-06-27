#include "network_input.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>

namespace {

std::string ToLower(std::string_view text)
{
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

std::string_view SchemeOf(std::string_view location)
{
    const auto pos = location.find("://");
    if (pos == std::string_view::npos)
        return {};
    return location.substr(0, pos);
}

bool IsSensitiveQueryKey(std::string_view key)
{
    const auto lower = ToLower(key);
    return lower == "token" || lower == "access_token" || lower == "signature";
}

void RedactSensitiveQueryValues(std::string& text, std::size_t queryStart)
{
    std::size_t pos = queryStart;
    while (pos < text.size()) {
        const auto keyStart = pos;
        auto equals = text.find('=', keyStart);
        auto next = text.find('&', keyStart);
        if (next == std::string::npos)
            next = text.size();

        if (equals != std::string::npos && equals < next && IsSensitiveQueryKey(std::string_view(text).substr(keyStart, equals - keyStart))) {
            text.replace(equals + 1, next - equals - 1, "***");
            next = text.find('&', equals + 1);
            if (next == std::string::npos)
                next = text.size();
        }

        if (next >= text.size())
            break;
        pos = next + 1;
    }
}

} // namespace

MediaSourceKind ClassifyMediaSource(std::string_view location)
{
    const auto scheme = ToLower(SchemeOf(location));
    if (scheme.empty())
        return MediaSourceKind::LocalFile;
    if (scheme == "http" || scheme == "https")
        return MediaSourceKind::Http;
    if (scheme == "rtsp")
        return MediaSourceKind::Rtsp;
    if (scheme == "rtp")
        return MediaSourceKind::Rtp;
    if (scheme == "udp")
        return MediaSourceKind::Udp;
    return MediaSourceKind::OtherNetwork;
}

bool IsNetworkSource(MediaSourceKind kind)
{
    return kind != MediaSourceKind::LocalFile;
}

bool IsRealtimeSource(MediaSourceKind kind)
{
    return kind == MediaSourceKind::Rtsp || kind == MediaSourceKind::Rtp || kind == MediaSourceKind::Udp;
}

ValidationResult ValidateMediaSource(const MediaSource& source)
{
    if (source.location.empty())
        return {false, "media location is empty"};
    if (source.network.connectTimeout.count() < 0)
        return {false, "connect timeout must not be negative"};
    if (source.network.readTimeout.count() < 0)
        return {false, "read timeout must not be negative"};
    if (source.network.analyzeDuration.count() <= 0)
        return {false, "analyze duration must be positive"};
    if (source.network.probeSize <= 0)
        return {false, "probe size must be positive"};
    if (source.network.maxReconnectAttempts < 0)
        return {false, "max reconnect attempts must not be negative"};
    if (source.network.initialReconnectDelay.count() < 0)
        return {false, "initial reconnect delay must not be negative"};
    if (source.network.maxReconnectDelay.count() < 0)
        return {false, "max reconnect delay must not be negative"};
    if (source.network.maxReconnectDelay < source.network.initialReconnectDelay)
        return {false, "max reconnect delay must be at least the initial delay"};
    return {true, {}};
}

std::chrono::milliseconds ReconnectDelay(const NetworkOptions& options, int attempt)
{
    if (attempt <= 0)
        return std::chrono::milliseconds{0};

    const auto initial = options.initialReconnectDelay.count();
    const auto maximum = options.maxReconnectDelay.count();
    if (initial <= 0)
        return std::chrono::milliseconds{0};

    std::int64_t delay = initial;
    for (int i = 1; i < attempt; ++i) {
        if (delay >= maximum || delay > std::numeric_limits<std::int64_t>::max() / 2) {
            delay = maximum;
            break;
        }
        delay *= 2;
    }
    return std::chrono::milliseconds{std::min(delay, maximum)};
}

bool ShouldReconnect(PlaybackError error)
{
    return error == PlaybackError::Timeout || error == PlaybackError::NetworkUnavailable || error == PlaybackError::ConnectionLost;
}

std::string RedactMediaLocation(std::string_view location)
{
    std::string result(location);

    const auto schemeEnd = result.find("://");
    const auto authorityStart = schemeEnd == std::string::npos ? 0 : schemeEnd + 3;
    const auto authorityEnd = result.find_first_of("/?#", authorityStart);
    const auto at = result.find('@', authorityStart);
    if (at != std::string::npos && (authorityEnd == std::string::npos || at < authorityEnd))
        result.replace(authorityStart, at - authorityStart, "***:***");

    const auto query = result.find('?');
    if (query != std::string::npos)
        RedactSensitiveQueryValues(result, query + 1);

    return result;
}
