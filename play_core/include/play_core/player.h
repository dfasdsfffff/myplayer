#pragma once

#include <play_core/media_source.h>
#include <play_core/video_frame.h>
#include <functional>
#include <memory>
#include <optional>

namespace play_core {
inline constexpr const char* Version = "1.0.0";
using SessionId = std::uint64_t;
using OperationId = std::uint64_t;
enum class PlayerState { Closed, Opening, Ready, Playing, Paused, Reconnecting, Stopped, Closing, Ended, Failed };
enum class ErrorCode {
    None, InvalidArgument, InvalidState, Capacity, Cancelled, Media, Internal,
    Timeout, Authentication, NotFound, UnsupportedProtocol, NetworkUnavailable,
    ConnectionLost, InvalidMedia, DecoderFailure
};
struct PlayerError { ErrorCode code{ErrorCode::None}; std::string message; int nativeCode{0}; };
struct Request {
    OperationId operation{0};
    SessionId session{0};
    PlayerError error;
    explicit operator bool() const noexcept { return operation != 0; }
};
enum class OperationStatus { Succeeded, Failed, Cancelled, Superseded };
struct OperationResult {
    OperationId operation{0};
    SessionId session{0};
    OperationStatus status{OperationStatus::Succeeded};
    PlayerError error;
    std::optional<std::int64_t> requestedPositionMs;
    // Demuxer seek target, not the timestamp of the first decoded/displayed frame.
    std::optional<std::int64_t> actualPositionMs;
};
enum class HardwareDecode { Auto, Disabled, D3D11VA };
struct PlayerOptions { HardwareDecode hardwareDecode{HardwareDecode::Auto}; bool loop{false}; };
struct PlayerEvent {
    enum class Type { State, MediaInfo, Position, Operation, Video, Subtitle, Ended, Error, Closed };
    Type type{Type::State};
    SessionId session{0};
    PlayerState state{PlayerState::Closed};
    std::shared_ptr<const MediaInfo> mediaInfo;
    std::int64_t positionMs{0};
    OperationResult operation;
    std::shared_ptr<const VideoFrame> video;
    std::shared_ptr<const SubtitleFrame> subtitle;
    PlayerError error;
};

// Move-only. reset()/destruction waits for in-flight callbacks, then cancels queued
// delivery. Never reset/destroy this subscription from its own callback.
class Subscription {
public:
    Subscription() noexcept;
    ~Subscription();
    Subscription(Subscription&&) noexcept;
    Subscription& operator=(Subscription&&) noexcept;
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    void reset() noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
    explicit Subscription(std::shared_ptr<State>);
    friend class Player;
};

// Methods copy their inputs and accept requests from any thread. No method may
// race destruction. Destruction joins core workers and event delivery; it must
// not run on an event callback. Construction/allocation may throw; callbacks
// are exception-isolated. Normal argument/state errors use Request/results.
class Player final {
public:
    explicit Player(PlayerOptions options = {});
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    Subscription subscribe(std::function<void(const PlayerEvent&)> callback);
    Request open(MediaSource source);
    Request play();
    Request setPaused(bool paused);
    Request seek(std::int64_t positionMs);
    Request stop();
    Request close();
    Request setHardwareDecode(HardwareDecode preference); // affects the next open
    Request setVolume(double volume); // [0,1]
    Request setPlaybackRate(double rate); // [0.1,2]
    Request selectAudioTrack(int trackId); // stream ID from MediaInfo
    Request selectSubtitleTrack(std::optional<int> trackId); // nullopt disables
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
