#include "media_format_registry.h"

#include <QTemporaryDir>

#include <iostream>

namespace {
bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

int main()
{
    const QStringList supportedLocations{
        "movie.MKV", "movie.rmvb", "movie.mp4", "movie.avi", "movie.flv", "movie.wmv", "movie.3gp",
        "movie.mov", "movie.m4v", "movie.webm", "movie.mpeg", "movie.mpg", "movie.ts", "movie.m2ts",
        "song.mp3", "song.aac", "song.m4a", "song.flac", "song.wav", "song.ogg", "song.opus",
        "C:/media/song.MP3",
        "https://example.test/live", "http://example.test/live", "rtsp://example.test/live",
        "rtp://example.test:5004", "udp://example.test:5004"
    };
    for (const QString& location : supportedLocations)
    {
        if (!Expect(IsSupportedMediaLocation(location), qPrintable("supported location: " + location)))
            return 1;
    }

    QTemporaryDir temporaryDirectory;
    if (!Expect(temporaryDirectory.isValid(), "temporary directory"))
        return 1;
    if (!Expect(!IsSupportedMediaLocation(temporaryDirectory.path()), "directories are rejected") ||
        !Expect(!IsSupportedMediaLocation("movie.unknown"), "unknown extensions are rejected") ||
        !Expect(!IsSupportedMediaLocation("ftp://example.test/movie.mp4"), "unsupported network schemes are rejected") ||
        !Expect(IsAudioOnlyMediaLocation("song.ogg"), "audio extensions identify audio-only playback") ||
        !Expect(!IsAudioOnlyMediaLocation("movie.webm"), "video containers do not identify audio-only playback"))
        return 1;

    const QString mediaFilter = MediaOpenDialogFilter();
    const QString subtitleFilter = SubtitleOpenDialogFilter();
    if (!Expect(mediaFilter.contains("*.mp3") && mediaFilter.contains("*.webm"), "media dialog includes audio and video formats") ||
        !Expect(subtitleFilter.contains("*.srt") && subtitleFilter.contains("*.ass") && subtitleFilter.contains("*.ssa"),
            "subtitle dialog includes supported subtitle formats"))
        return 1;

    return 0;
}
