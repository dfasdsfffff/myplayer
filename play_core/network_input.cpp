#include "network_input.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <limits>
#include <sstream>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/time.h>
}

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
    return lower == "token" || lower == "access_token" || lower == "auth" || lower == "key"
        || lower == "signature" || lower == "sig";
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

std::string MicrosecondsString(std::chrono::microseconds value)
{
    return std::to_string(value.count());
}

std::string MillisecondsAsMicrosecondsString(std::chrono::milliseconds value)
{
    return MicrosecondsString(std::chrono::duration_cast<std::chrono::microseconds>(value));
}

void SetOption(AVDictionary** dictionary, const char* key, const std::string& value)
{
    av_dict_set(dictionary, key, value.c_str(), 0);
}

void SetCommonProbeOptions(AVDictionary** dictionary, const NetworkOptions& options)
{
    SetOption(dictionary, "probesize", std::to_string(options.probeSize));
    SetOption(dictionary, "analyzeduration", MicrosecondsString(options.analyzeDuration));
}

std::string BuildHeaderBlock(const std::map<std::string, std::string>& headers)
{
    std::ostringstream stream;
    for (const auto& [name, value] : headers) {
        if (!name.empty())
            stream << name << ": " << value << "\r\n";
    }
    return stream.str();
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

AvDictionary::~AvDictionary()
{
    av_dict_free(&m_dictionary);
}

AvDictionary::AvDictionary(AvDictionary&& other) noexcept
    : m_dictionary(other.m_dictionary)
{
    other.m_dictionary = nullptr;
}

AvDictionary& AvDictionary::operator=(AvDictionary&& other) noexcept
{
    if (this != &other) {
        av_dict_free(&m_dictionary);
        m_dictionary = other.m_dictionary;
        other.m_dictionary = nullptr;
    }
    return *this;
}

AVDictionary* AvDictionary::get() const noexcept
{
    return m_dictionary;
}

AVDictionary** AvDictionary::put() noexcept
{
    return &m_dictionary;
}

void IoControl::begin(IoOperation newOperation, std::chrono::milliseconds timeout)
{
    operation.store(newOperation, std::memory_order_release);
    if (timeout.count() <= 0) {
        deadlineUs.store(0, std::memory_order_release);
        return;
    }
    const auto timeoutUs = std::chrono::duration_cast<std::chrono::microseconds>(timeout).count();
    deadlineUs.store(av_gettime_relative() + timeoutUs, std::memory_order_release);
}

void IoControl::end()
{
    deadlineUs.store(0, std::memory_order_release);
    operation.store(IoOperation::None, std::memory_order_release);
}

AvDictionary BuildInputOptions(const MediaSource& source)
{
    AvDictionary dictionary;
    AVDictionary** options = dictionary.put();
    SetCommonProbeOptions(options, source.network);

    const auto kind = ClassifyMediaSource(source.location);
    if (!IsNetworkSource(kind))
        return dictionary;

    SetOption(options, "rw_timeout", MillisecondsAsMicrosecondsString(source.network.readTimeout));

    if (kind == MediaSourceKind::Http) {
        if (!source.network.userAgent.empty())
            SetOption(options, "user_agent", source.network.userAgent);
        const auto headers = BuildHeaderBlock(source.network.headers);
        if (!headers.empty())
            SetOption(options, "headers", headers);
        if (source.network.reconnect) {
            SetOption(options, "reconnect", "1");
            SetOption(options, "reconnect_streamed", "1");
            SetOption(options, "reconnect_on_network_error", "1");
            SetOption(options, "reconnect_on_http_error", "500,502,503,504");
        }
    } else if (kind == MediaSourceKind::Rtsp) {
        SetOption(options, "rtsp_transport", source.network.rtspTransport == RtspTransport::Udp ? "udp" : "tcp");
        SetOption(options, "timeout", MillisecondsAsMicrosecondsString(source.network.connectTimeout));
    }

    return dictionary;
}

PlaybackError MapAvError(int avError, bool realtime)
{
    if (avError == AVERROR_EXIT)
        return PlaybackError::Cancelled;
    if (avError == AVERROR(ETIMEDOUT))
        return PlaybackError::Timeout;
    if (avError == AVERROR_HTTP_UNAUTHORIZED || avError == AVERROR_HTTP_FORBIDDEN)
        return PlaybackError::Authentication;
    if (avError == AVERROR_HTTP_NOT_FOUND)
        return PlaybackError::NotFound;
    if (avError == AVERROR_PROTOCOL_NOT_FOUND)
        return PlaybackError::UnsupportedProtocol;
    if (avError == AVERROR_EOF)
        return realtime ? PlaybackError::ConnectionLost : PlaybackError::None;
    if (avError == AVERROR_INVALIDDATA)
        return PlaybackError::InvalidMedia;
    if (avError == AVERROR(ECONNRESET) || avError == AVERROR(ECONNREFUSED) || avError == AVERROR(EHOSTUNREACH)
        || avError == AVERROR(ENETUNREACH))
        return PlaybackError::NetworkUnavailable;
    return PlaybackError::Unknown;
}

int InterruptNetworkIo(void* opaque)
{
    auto* control = static_cast<IoControl*>(opaque);
    if (!control)
        return 0;
    if (control->cancelled.load(std::memory_order_acquire))
        return 1;

    const auto deadline = control->deadlineUs.load(std::memory_order_acquire);
    if (deadline == 0)
        return 0;
    return av_gettime_relative() >= deadline ? 1 : 0;
}

MediaInfo BuildMediaInfo(const MediaSource& source, AVFormatContext* formatContext)
{
    const auto kind = ClassifyMediaSource(source.location);
    MediaInfo info;
    info.networkSource = IsNetworkSource(kind);
    info.live = IsRealtimeSource(kind);

    if (formatContext) {
        if (formatContext->duration > 0 && !info.live)
            info.duration = std::chrono::milliseconds{formatContext->duration / 1000};
        info.seekable = !info.live && formatContext->pb && (formatContext->pb->seekable & AVIO_SEEKABLE_NORMAL);
        for (unsigned int index = 0; index < formatContext->nb_streams; ++index) {
            const AVStream* stream = formatContext->streams[index];
            const AVCodecParameters* parameters = stream->codecpar;
            if (parameters->codec_type == AVMEDIA_TYPE_VIDEO) {
                info.width = parameters->width;
                info.height = parameters->height;
                if (stream->sample_aspect_ratio.num > 0 && stream->sample_aspect_ratio.den > 0)
                    info.sampleAspectRatio = stream->sample_aspect_ratio;
                continue;
            }
            if (parameters->codec_type != AVMEDIA_TYPE_AUDIO && parameters->codec_type != AVMEDIA_TYPE_SUBTITLE)
                continue;
            TrackInfo track;
            track.streamIndex = static_cast<int>(index);
            track.type = parameters->codec_type;
            if (const AVDictionaryEntry* entry = av_dict_get(stream->metadata, "language", nullptr, 0))
                track.language = entry->value;
            if (const AVDictionaryEntry* entry = av_dict_get(stream->metadata, "title", nullptr, 0))
                track.title = entry->value;
            track.codec = avcodec_get_name(parameters->codec_id);
            track.isDefault = (stream->disposition & AV_DISPOSITION_DEFAULT) != 0;
            track.isForced = (stream->disposition & AV_DISPOSITION_FORCED) != 0;
            info.tracks.push_back(std::move(track));
        }
    } else {
        info.seekable = !info.live && !info.networkSource;
    }

    return info;
}

bool CanSeek(const MediaInfo& info)
{
    return info.seekable;
}

bool UseUnlimitedBuffer(const MediaSource& source, bool formatRealtime)
{
    return formatRealtime || IsRealtimeSource(ClassifyMediaSource(source.location));
}
