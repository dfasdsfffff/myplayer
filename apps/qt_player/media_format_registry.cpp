#include "media_format_registry.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QUrl>

namespace {
const QSet<QString>& SupportedExtensions()
{
    static const QSet<QString> extensions{
        "mkv", "rmvb", "mp4", "avi", "flv", "wmv", "3gp",
        "mov", "m4v", "webm", "mpeg", "mpg", "ts", "m2ts",
        "mp3", "aac", "m4a", "flac", "wav", "ogg", "opus"
    };
    return extensions;
}

bool IsSupportedNetworkScheme(const QString& scheme)
{
    static const QSet<QString> schemes{"http", "https", "rtsp", "rtp", "udp"};
    return schemes.contains(scheme);
}

bool HasSupportedExtension(const QString& location)
{
    const QFileInfo fileInfo(location);
    return !fileInfo.isDir() && SupportedExtensions().contains(fileInfo.suffix().toLower());
}
}

bool IsSupportedMediaLocation(const QString& location)
{
    const QUrl url(location);
    if (url.isLocalFile())
        return HasSupportedExtension(url.toLocalFile());

    if (!QDir::isAbsolutePath(location) && url.isValid() && !url.scheme().isEmpty())
        return IsSupportedNetworkMediaLocation(location);

    return HasSupportedExtension(location);
}

bool IsSupportedNetworkMediaLocation(const QString& location)
{
    if (QDir::isAbsolutePath(location))
        return false;

    const QUrl url(location);
    return url.isValid() && IsSupportedNetworkScheme(url.scheme().toLower());
}

bool IsAudioOnlyMediaLocation(const QString& location)
{
    const QUrl url(location);
    const QString localPath = url.isLocalFile() ? url.toLocalFile() : location;
    static const QSet<QString> audioExtensions{"mp3", "aac", "m4a", "flac", "wav", "ogg", "opus"};
    return audioExtensions.contains(QFileInfo(localPath).suffix().toLower());
}

QString MediaOpenDialogFilter()
{
    return "媒体文件 (*.mkv *.rmvb *.mp4 *.avi *.flv *.wmv *.3gp *.mov *.m4v *.webm *.mpeg *.mpg *.ts *.m2ts *.mp3 *.aac *.m4a *.flac *.wav *.ogg *.opus)";
}

QString SubtitleOpenDialogFilter()
{
    return "字幕文件 (*.srt *.ass *.ssa)";
}
