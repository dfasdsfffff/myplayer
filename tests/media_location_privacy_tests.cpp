#include "media_location_privacy.h"

#include <iostream>

namespace {
bool Expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

int main()
{
    if (!Expect(ContainsSensitiveMediaCredentials("rtsp://alice:secret@example.com/live"), "URL userinfo is sensitive")) return 1;
    if (!Expect(ContainsSensitiveMediaCredentials("https://example.com/live?ACCESS_TOKEN=hidden"), "query keys are case insensitive")) return 1;
    if (!Expect(ContainsSensitiveMediaCredentials("https://example.com/live?signature=hidden"), "signature is sensitive")) return 1;
    if (!Expect(!MayPersistMediaLocation("https://example.com/live?token=hidden"), "token URL is not persistable")) return 1;
    if (!Expect(MayPersistMediaLocation("rtsp://example.com/live"), "public RTSP is persistable")) return 1;
    if (!Expect(MayPersistMediaLocation("C:/media/movie.mp4"), "local path is persistable")) return 1;
    if (!Expect(!SafeMediaLocationForDisplay("https://alice:secret@example.com/live?key=value").contains("secret"), "display redacts credentials")) return 1;
    return 0;
}
