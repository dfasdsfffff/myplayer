#define SDL_MAIN_HANDLED

#include "media_fixture_builder.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

class LoopbackHttpServer final {
public:
    explicit LoopbackHttpServer(std::vector<char> media)
        : m_media(std::move(media))
    {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            throw std::runtime_error("WSAStartup failed");
        m_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listener == INVALID_SOCKET)
            throw std::runtime_error("socket failed");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(m_listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
            listen(m_listener, SOMAXCONN) == SOCKET_ERROR)
            throw std::runtime_error("loopback bind failed");
        int length = sizeof(address);
        getsockname(m_listener, reinterpret_cast<sockaddr*>(&address), &length);
        m_port = ntohs(address.sin_port);
        m_thread = std::thread([this] { serve(); });
    }

    ~LoopbackHttpServer()
    {
        m_stopping = true;
        shutdown(m_listener, SD_BOTH);
        closesocket(m_listener);
        if (m_thread.joinable())
            m_thread.join();
        WSACleanup();
    }

    [[nodiscard]] std::string url(const char* path) const
    {
        return "http://127.0.0.1:" + std::to_string(m_port) + path;
    }

private:
    static void sendAll(SOCKET client, const std::string& value)
    {
        send(client, value.data(), static_cast<int>(value.size()), 0);
    }

    void serve()
    {
        while (!m_stopping) {
            SOCKET client = accept(m_listener, nullptr, nullptr);
            if (client == INVALID_SOCKET)
                break;
            char request[1024]{};
            const int bytes = recv(client, request, sizeof(request) - 1, 0);
            const std::string path = bytes > 0 ? std::string(request, bytes) : std::string{};
            if (path.find("GET /delayed ") != std::string::npos)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (path.find("GET /drop ") != std::string::npos) {
                closesocket(client);
                continue;
            }
            if (path.find("GET /missing ") != std::string::npos) {
                sendAll(client, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            } else if (path.find("GET /auth ") != std::string::npos) {
                sendAll(client, "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"test\"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            } else {
                sendAll(client, "HTTP/1.1 200 OK\r\nContent-Type: video/x-msvideo\r\nContent-Length: " +
                    std::to_string(m_media.size()) + "\r\nConnection: close\r\n\r\n");
                send(client, m_media.data(), static_cast<int>(m_media.size()), 0);
            }
            closesocket(client);
        }
    }

    SOCKET m_listener{INVALID_SOCKET};
    unsigned short m_port{};
    std::vector<char> m_media;
    std::thread m_thread;
    std::atomic_bool m_stopping{false};
};

int StatusCode(const std::string& url)
{
    const auto colon = url.rfind(':');
    const auto slash = url.find('/', colon);
    const unsigned short port = static_cast<unsigned short>(std::stoi(url.substr(colon + 1, slash - colon - 1)));
    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
        return 0;
    const std::string request = "GET " + url.substr(slash) + " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    send(client, request.data(), static_cast<int>(request.size()), 0);
    char response[128]{};
    const int received = recv(client, response, sizeof(response) - 1, 0);
    closesocket(client);
    if (received <= 0)
        return 0;
    int status = 0;
    std::sscanf(response, "HTTP/%*d.%*d %d", &status);
    return status;
}

} // namespace

int main()
{
    const GeneratedMediaFixtures fixtures = BuildMediaFixtures(std::filesystem::temp_directory_path() / "myplayer-network-fixtures");
    std::ifstream input(fixtures.video, std::ios::binary);
    const std::vector<char> media((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    LoopbackHttpServer server(media);
    if (!Expect(StatusCode(server.url("/video")) == 200, "loopback server serves generated media") ||
        !Expect(StatusCode(server.url("/delayed")) == 200, "loopback server supports delayed reads") ||
        !Expect(StatusCode(server.url("/missing")) == 404, "loopback server exposes 404 failures") ||
        !Expect(StatusCode(server.url("/auth")) == 401, "loopback server exposes authentication failures") ||
        !Expect(StatusCode(server.url("/drop")) == 0, "loopback server can disconnect during a read"))
        return 1;
    return 0;
}
