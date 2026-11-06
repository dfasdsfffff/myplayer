#pragma once
namespace play_core {
// Optional shared FFmpeg network initialization lease for hosts that also use
// FFmpeg directly (for example a recorder). No playback instance is created.
// Construction throws std::runtime_error on initialization failure.
class NetworkRuntime final {
public:
    NetworkRuntime();
    ~NetworkRuntime();
    NetworkRuntime(const NetworkRuntime&) = delete;
    NetworkRuntime& operator=(const NetworkRuntime&) = delete;
};
}
