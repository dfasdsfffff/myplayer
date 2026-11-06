#pragma once
#include <play_core/player.h>
using MediaSource = play_core::MediaSource;
using NetworkOptions = play_core::NetworkOptions;
using RtspTransport = play_core::RtspTransport;
using MediaInfo = play_core::MediaInfo;
using TrackInfo = play_core::TrackInfo;
inline constexpr auto AVMEDIA_TYPE_AUDIO = play_core::MediaType::Audio;
inline constexpr auto AVMEDIA_TYPE_VIDEO = play_core::MediaType::Video;
inline constexpr auto AVMEDIA_TYPE_SUBTITLE = play_core::MediaType::Subtitle;
// Presentation model retained by the application's status UI, not a Core API.
enum class PlaybackState { Opening, Buffering, Reconnecting, Playing, Failed, Stopped };
enum class PlaybackError { None, Cancelled, Timeout, Authentication, NotFound, UnsupportedProtocol, NetworkUnavailable, ConnectionLost, InvalidMedia, DecoderFailure, Unknown };
struct PlaybackStatus {
    PlaybackState state{PlaybackState::Stopped}; PlaybackError error{PlaybackError::None};
    int reconnectAttempt{0}, maxReconnectAttempts{0};
    std::chrono::milliseconds retryAfter{0}; std::string message;
};
