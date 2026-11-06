#define SDL_MAIN_HANDLED

#include "media_fixture_builder.h"
#include "playback_runtime.h"
#include <play_core/player.h>
#include <map>

#include <winsock2.h>
#include <ws2tcpip.h>

extern "C" {
#include <libavformat/avformat.h>
}

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

class FixtureDirectory final {
public:
    FixtureDirectory()
    {
        const auto stamp = Clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            path = std::filesystem::temp_directory_path() /
                ("myplayer-network-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(path))
                return;
        }
        throw std::runtime_error("cannot reserve network fixture directory");
    }
    ~FixtureDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};

// AVI's index probing is unsuitable for a response deliberately held open.
// Remux the generated frames into a genuinely non-seekable transport stream.
std::vector<char> StreamingMedia(const std::filesystem::path& inputPath, const std::filesystem::path& outputPath)
{
    AVFormatContext* input = nullptr;
    AVFormatContext* output = nullptr;
    AVPacket* packet = av_packet_alloc();
    auto cleanup = [&] {
        av_packet_free(&packet);
        avformat_close_input(&input);
        if (output && output->pb)
            avio_closep(&output->pb);
        avformat_free_context(output);
    };
    try {
        if (!packet || avformat_open_input(&input, inputPath.string().c_str(), nullptr, nullptr) < 0 ||
            avformat_find_stream_info(input, nullptr) < 0 ||
            avformat_alloc_output_context2(&output, nullptr, "mpegts", outputPath.string().c_str()) < 0)
            throw std::runtime_error("cannot initialize streaming fixture");
        auto* stream = avformat_new_stream(output, nullptr);
        if (!stream || avcodec_parameters_copy(stream->codecpar, input->streams[0]->codecpar) < 0)
            throw std::runtime_error("cannot copy streaming codec parameters");
        stream->codecpar->codec_tag = 0;
        stream->time_base = input->streams[0]->time_base;
        if (avio_open(&output->pb, outputPath.string().c_str(), AVIO_FLAG_WRITE) < 0 ||
            avformat_write_header(output, nullptr) < 0)
            throw std::runtime_error("cannot write streaming fixture header");
        int result;
        while ((result = av_read_frame(input, packet)) >= 0) {
            av_packet_rescale_ts(packet, input->streams[0]->time_base, stream->time_base);
            packet->stream_index = 0;
            if (av_interleaved_write_frame(output, packet) < 0)
                throw std::runtime_error("cannot write streaming packet");
        }
        if (result != AVERROR_EOF || av_write_trailer(output) < 0)
            throw std::runtime_error("cannot finalize streaming fixture");
    } catch (...) {
        cleanup();
        throw;
    }
    cleanup();
    std::ifstream file(outputPath, std::ios::binary);
    if (!file)
        throw std::runtime_error("cannot read transport fixture");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

enum class Response { Complete, NotFound, Authentication, StallHeaders, StallBody };

// Only loopback sockets are used. Shutdown wakes both accept/recv and the
// condition-variable gate, so a failed assertion cannot strand the server.
class LoopbackHttpServer final {
public:
    LoopbackHttpServer(const std::vector<char>& media, Response response)
        : m_media(media), m_response(response)
    {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            throw std::runtime_error("WSAStartup failed");
        try {
            m_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (m_listener == INVALID_SOCKET)
                throw std::runtime_error("socket failed");
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (bind(m_listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
                listen(m_listener, SOMAXCONN) == SOCKET_ERROR)
                throw std::runtime_error("loopback bind/listen failed");
            int length = sizeof(address);
            if (getsockname(m_listener, reinterpret_cast<sockaddr*>(&address), &length) == SOCKET_ERROR)
                throw std::runtime_error("getsockname failed");
            m_port = ntohs(address.sin_port);
            m_thread = std::thread([this] { serve(); });
        } catch (...) {
            if (m_listener != INVALID_SOCKET)
                closesocket(m_listener);
            WSACleanup();
            throw;
        }
    }

    ~LoopbackHttpServer()
    {
        {
            std::lock_guard lock(m_mutex);
            m_stopping = true;
            if (m_client != INVALID_SOCKET)
                shutdown(m_client, SD_BOTH);
        }
        m_changed.notify_all();
        shutdown(m_listener, SD_BOTH);
        closesocket(m_listener);
        m_thread.join();
        WSACleanup();
    }

    std::string url() const { return "http://127.0.0.1:" + std::to_string(m_port) + "/video"; }

    bool waitForRequest()
    {
        std::unique_lock lock(m_mutex);
        return Expect(m_changed.wait_for(lock, 5s, [&] { return m_requests > 0; }),
                      "player reaches the loopback request gate");
    }

    void disconnectClient()
    {
        std::lock_guard lock(m_mutex);
        m_disconnect = true;
        m_changed.notify_all();
    }

private:
    static bool sendAll(SOCKET client, const char* data, std::size_t size)
    {
        while (size > 0) {
            const int bytes = send(client, data, static_cast<int>((std::min)(size, std::size_t{16384})), 0);
            if (bytes <= 0)
                return false;
            data += bytes;
            size -= bytes;
        }
        return true;
    }

    static bool sendText(SOCKET client, const std::string& value)
    {
        return sendAll(client, value.data(), value.size());
    }

    void serve()
    {
        for (;;) {
            SOCKET client = accept(m_listener, nullptr, nullptr);
            if (client == INVALID_SOCKET)
                return;
            {
                std::lock_guard lock(m_mutex);
                if (m_stopping) {
                    closesocket(client);
                    return;
                }
                m_client = client;
            }
            const DWORD timeout = 1000;
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
            std::string request;
            char buffer[1024];
            while (request.size() < 16384 && request.find("\r\n\r\n") == std::string::npos) {
                const int bytes = recv(client, buffer, sizeof(buffer), 0);
                if (bytes <= 0)
                    break;
                request.append(buffer, bytes);
            }
            if (m_response == Response::NotFound) {
                sendText(client, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            } else if (m_response == Response::Authentication) {
                sendText(client, "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"test\"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            } else if (m_response == Response::Complete) {
                if (sendText(client, "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\nContent-Length: " +
                    std::to_string(m_media.size()) + "\r\nConnection: close\r\n\r\n"))
                    sendAll(client, m_media.data(), m_media.size());
            } else if (m_response == Response::StallBody) {
                // No Content-Length: FFmpeg must treat this as a non-seekable stream.
                if (sendText(client, "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\nConnection: close\r\n\r\n"))
                    sendAll(client, m_media.data(), m_media.size());
            }
            {
                std::unique_lock lock(m_mutex);
                ++m_requests;
                m_changed.notify_all();
                if (m_response == Response::StallHeaders || m_response == Response::StallBody)
                    m_changed.wait(lock, [&] { return m_stopping || m_disconnect; });
                if (m_disconnect) {
                    const linger reset{1, 0};
                    setsockopt(client, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&reset), sizeof(reset));
                }
                closesocket(client);
                m_client = INVALID_SOCKET;
                if (m_stopping)
                    return;
            }
        }
    }

    const std::vector<char>& m_media;
    Response m_response;
    SOCKET m_listener{INVALID_SOCKET};
    SOCKET m_client{INVALID_SOCKET};
    unsigned short m_port{};
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_changed;
    bool m_stopping{false};
    bool m_disconnect{false};
    int m_requests{0};
};

struct Observation {
    std::vector<PlaybackStatus> statuses;
    int frames{0};
    int completions{0};
};

class PlaybackObserver final {
public:
    void connect(PlaybackRuntime& runtime)
    {
        m_connections.emplace_back(runtime.SigPlaybackStatus.connect([this](const PlaybackStatus& status) {
            std::lock_guard lock(m_mutex);
            m_value.statuses.push_back(status);
            if (status.state == PlaybackState::Failed)
                std::cout << "Network failure category=" << static_cast<int>(status.error) << '\n';
            m_changed.notify_all();
        }));
        m_connections.emplace_back(runtime.SigVideoFrame.connect([this](const std::shared_ptr<VideoFrame>& frame) {
            if (frame) {
                std::lock_guard lock(m_mutex);
                ++m_value.frames;
                m_changed.notify_all();
            }
        }));
        m_connections.emplace_back(runtime.SigStopFinished.connect([this] {
            std::lock_guard lock(m_mutex);
            ++m_value.completions;
            m_changed.notify_all();
        }));
    }

    template<typename Predicate>
    bool wait(Predicate predicate, const char* message)
    {
        std::unique_lock lock(m_mutex);
        return Expect(m_changed.wait_for(lock, 5s, [&] { return predicate(m_value); }), message);
    }

    Observation snapshot()
    {
        std::lock_guard lock(m_mutex);
        return m_value;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    Observation m_value;
    std::vector<sigslot::scoped_connection> m_connections;
};

bool HasState(const Observation& value, PlaybackState state)
{
    return std::any_of(value.statuses.begin(), value.statuses.end(), [=](const auto& status) { return status.state == state; });
}

MediaSource SourceFor(const LoopbackHttpServer& server)
{
    MediaSource source{server.url()};
    source.network.reconnect = false;
    source.network.connectTimeout = 2s;
    source.network.readTimeout = 2s;
    source.network.analyzeDuration = 100ms;
    source.network.probeSize = 8192;
    return source;
}

bool FailureCase(const std::vector<char>& media, Response response, PlaybackError expected, bool allowReconnect)
{
    LoopbackHttpServer server(media, response);
    PlaybackObserver observer; // Must outlive runtime callbacks, including failure cleanup.
    auto runtime = PlaybackRuntime::Create();
    if (!Expect(runtime != nullptr, "runtime initializes for HTTP failure"))
        return false;
    observer.connect(*runtime);
    auto source = SourceFor(server);
    source.network.reconnect = allowReconnect;
    source.network.maxReconnectAttempts = 2;
    if (response == Response::StallHeaders)
        source.network.connectTimeout = 150ms;
    if (!Expect(runtime->controller().play(source), "network open starts asynchronously") ||
        !observer.wait([](const auto& value) { return HasState(value, PlaybackState::Failed) && value.completions == 1; },
                       "HTTP failure publishes status and completes its session"))
        return false;
    runtime->controller().stopAndWait();
    const auto value = observer.snapshot();
    const auto failure = std::find_if(value.statuses.begin(), value.statuses.end(), [](const auto& status) {
        return status.state == PlaybackState::Failed;
    });
    if (failure != value.statuses.end() && failure->error != expected)
        std::cerr << "Observed error=" << static_cast<int>(failure->error) << ", expected=" << static_cast<int>(expected) << '\n';
    return Expect(failure != value.statuses.end() && failure->error == expected, "HTTP failure exposes the correct error category") &&
           Expect(!HasState(value, PlaybackState::Reconnecting), "terminal HTTP errors or disabled retries do not reconnect") &&
           Expect(!HasState(value, PlaybackState::Playing) && value.frames == 0, "failed open never reports playback or frames");
}

bool StopCase(const std::vector<char>& media, Response response)
{
    LoopbackHttpServer server(media, response);
    PlaybackObserver observer;
    auto runtime = PlaybackRuntime::Create();
    if (!Expect(runtime != nullptr, "runtime initializes for blocked network stop"))
        return false;
    observer.connect(*runtime);
    auto source = SourceFor(server);
    source.network.connectTimeout = 30s;
    source.network.readTimeout = 30s;
    if (!Expect(runtime->controller().play(source), "stalled source starts") || !server.waitForRequest())
        return false;
    if (response == Response::StallBody &&
        !observer.wait([](const auto& value) { return HasState(value, PlaybackState::Playing) && value.frames > 0; },
                       "HTTP body stall occurs after actual video playback starts"))
        return false;
    const auto start = Clock::now();
    runtime->controller().stop();
    runtime->controller().stopAndWait();
    const auto elapsed = Clock::now() - start;
    const auto value = observer.snapshot();
    return Expect(elapsed < 1500ms, "stop interrupts network I/O instead of waiting for its 30-second timeout") &&
           Expect(value.completions == 1, "network stop joins exactly one completed session") &&
           Expect(HasState(value, PlaybackState::Stopped), "explicit network stop publishes Stopped") &&
           Expect(!HasState(value, PlaybackState::Failed), "user cancellation is not reported as playback failure");
}

bool DisconnectCase(const std::vector<char>& media)
{
    LoopbackHttpServer server(media, Response::StallBody);
    PlaybackObserver observer;
    auto runtime = PlaybackRuntime::Create();
    if (!Expect(runtime != nullptr, "runtime initializes for disconnect"))
        return false;
    observer.connect(*runtime);
    auto source = SourceFor(server);
    if (!Expect(runtime->controller().play(source), "disconnect source starts") || !server.waitForRequest() ||
        !observer.wait([](const auto& value) { return HasState(value, PlaybackState::Playing) && value.frames > 0; },
                       "video plays before the server disconnects"))
        return false;
    server.disconnectClient();
    if (!observer.wait([](const auto& value) { return value.completions == 1; }, "disconnect completes the playback session"))
        return false;
    runtime->controller().stopAndWait();
    const auto value = observer.snapshot();
    return Expect(std::any_of(value.statuses.begin(), value.statuses.end(), [](const auto& status) {
        return status.state == PlaybackState::Failed &&
            (status.error == PlaybackError::NetworkUnavailable || status.error == PlaybackError::ConnectionLost);
    }), "connection reset during playback is reported as a network failure");
}

bool ReadTimeoutCase(const std::vector<char>& media)
{
    LoopbackHttpServer server(media, Response::StallBody);
    PlaybackObserver observer;
    auto runtime = PlaybackRuntime::Create();
    if (!Expect(runtime != nullptr, "runtime initializes for read timeout"))
        return false;
    observer.connect(*runtime);
    auto source = SourceFor(server);
    source.network.readTimeout = 150ms;
    if (!Expect(runtime->controller().play(source), "read-timeout source starts") ||
        !observer.wait([](const auto& value) { return value.completions == 1; },
                       "stalled body read terminates the playback session"))
        return false;
    runtime->controller().stopAndWait();
    const auto value = observer.snapshot();
    return Expect(HasState(value, PlaybackState::Playing) && value.frames > 0,
                  "read timeout follows real video playback, not a failed open") &&
           Expect(std::any_of(value.statuses.begin(), value.statuses.end(), [](const auto& status) {
               return status.state == PlaybackState::Failed && status.error == PlaybackError::Timeout;
           }), "stalled body read is classified as Timeout");
}

bool ReconnectStopCase(const std::vector<char>& media)
{
    LoopbackHttpServer server(media, Response::StallHeaders);
    PlaybackObserver observer;
    auto runtime = PlaybackRuntime::Create();
    if (!Expect(runtime != nullptr, "runtime initializes for reconnect cancellation"))
        return false;
    observer.connect(*runtime);
    auto source = SourceFor(server);
    source.network.reconnect = true;
    source.network.connectTimeout = 150ms;
    source.network.initialReconnectDelay = 10s;
    source.network.maxReconnectDelay = 10s;
    if (!Expect(runtime->controller().play(source), "timeout source starts with reconnect enabled") ||
        !observer.wait([](const auto& value) { return HasState(value, PlaybackState::Reconnecting); }, "open timeout enters reconnect backoff"))
        return false;
    const auto start = Clock::now();
    runtime->controller().stop();
    runtime->controller().stopAndWait();
    const auto value = observer.snapshot();
    return Expect(Clock::now() - start < 1500ms, "stop cancels ten-second reconnect backoff promptly") &&
           Expect(value.completions == 1, "cancelled reconnect completes exactly one playback session") &&
           Expect(HasState(value, PlaybackState::Stopped), "cancelled reconnect publishes Stopped");
}

} // namespace

bool CompleteCase(const std::vector<char>& media)
{
    LoopbackHttpServer server(media, Response::Complete);
    PlaybackObserver observer;
    auto runtime = PlaybackRuntime::Create();
    if (!Expect(runtime != nullptr, "runtime initializes for complete HTTP video"))
        return false;
    observer.connect(*runtime);
    runtime->controller().setLoopPolicy(LOOP_NONE);
    if (!Expect(runtime->controller().play(SourceFor(server)), "complete HTTP video starts") ||
        !observer.wait([](const auto& value) { return value.completions == 1; }, "finite HTTP video reaches natural completion"))
        return false;
    runtime->controller().stopAndWait();
    const auto value = observer.snapshot();
    return Expect(value.frames > 0 && HasState(value, PlaybackState::Playing), "finite HTTP video actually plays") &&
           Expect(!HasState(value, PlaybackState::Failed), "normal HTTP end-of-media is not a network failure");
}

bool SdkNetworkCase(const std::vector<char>& media, bool reconnect)
{
    LoopbackHttpServer server(media, Response::StallHeaders);
    play_core::Player player;
    std::mutex mutex; std::condition_variable cv;
    std::map<play_core::OperationId,play_core::OperationResult> results;
    bool retrying = false;
    auto subscription = player.subscribe([&](const auto& e) {
        std::lock_guard lock(mutex);
        if (e.type == play_core::PlayerEvent::Type::Operation) results[e.operation.operation] = e.operation;
        if (e.type == play_core::PlayerEvent::Type::State && e.state == play_core::PlayerState::Reconnecting) retrying = true;
        cv.notify_all();
    });
    play_core::MediaSource source{server.url()};
    source.network.reconnect = reconnect;
    source.network.connectTimeout = reconnect ? 150ms : 10s;
    source.network.initialReconnectDelay = 10s; source.network.maxReconnectDelay = 10s;
    const auto opening = player.open(source);
    if (!server.waitForRequest()) return false;
    if (reconnect) {
        std::unique_lock lock(mutex);
        if (!Expect(cv.wait_for(lock,5s,[&] { return retrying; }),"SDK enters reconnect")) return false;
    }
    const auto start = Clock::now(); const auto closing = player.close();
    std::unique_lock lock(mutex);
    return Expect(cv.wait_for(lock,1500ms,[&] { return results.contains(opening.operation) && results.contains(closing.operation); }),"SDK closes opening/reconnecting promptly") &&
        Expect(Clock::now()-start < 1500ms,"SDK close cancels network blocking work") &&
        Expect(results.at(opening.operation).status == play_core::OperationStatus::Cancelled,"SDK settles interrupted open once") &&
        Expect(results.at(closing.operation).status == play_core::OperationStatus::Succeeded,"SDK closes successfully");
}

int main()
{
    try {
        FixtureDirectory directory;
        const auto fixtures = BuildMediaFixtures(directory.path, 40);
        const auto media = StreamingMedia(fixtures.video, directory.path / "stream.ts");
        // Run independent cases even after failure, to expose all behavioral gaps.
        bool passed = true;
        auto run = [&](const char* name, bool result) {
            std::cout << (result ? "PASSED: " : "FAILED: ") << name << '\n';
            passed = result && passed;
        };
        run("HTTP 404 without retry", FailureCase(media, Response::NotFound, PlaybackError::NotFound, true));
        run("HTTP 401 without retry", FailureCase(media, Response::Authentication, PlaybackError::Authentication, true));
        run("open timeout", FailureCase(media, Response::StallHeaders, PlaybackError::Timeout, false));
        run("stop during open", StopCase(media, Response::StallHeaders));
        run("stop during streaming", StopCase(media, Response::StallBody));
        run("connection reset after video frames", DisconnectCase(media));
        run("read timeout after video frames", ReadTimeoutCase(media));
        run("stop during reconnect backoff", ReconnectStopCase(media));
        run("normal HTTP end-of-media", CompleteCase(media));
        run("SDK close while opening", SdkNetworkCase(media,false));
        run("SDK close while reconnecting", SdkNetworkCase(media,true));
        return passed ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Network acceptance failed: " << error.what() << '\n';
        return 1;
    }
}
