#include "playlistfile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include <iostream>

namespace {
bool WriteTextFile(const QString& fileName, const QString& content)
{
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << content;
    return true;
}

bool TouchFile(const QString& fileName)
{
    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly))
        return true;

    std::cerr << "FAILED: create file " << fileName.toStdString()
              << " in temp dir " << QFileInfo(fileName).absolutePath().toStdString()
              << " with QFile: " << file.errorString().toStdString() << '\n';
    return false;
}

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tempDir(QDir::current().filePath("playlistfile_tests-XXXXXX"));
    if (!Expect(tempDir.isValid(), "temporary directory should be valid"))
        return 1;

    QDir dir(tempDir.path());
    const QString first = dir.filePath("001. Project Overview.mp4");
    const QString second = dir.filePath("002. Demo Show.mp4");
    const QString third = dir.filePath("003. Relative Video.avi");

    if (!Expect(TouchFile(first), "create first media file") ||
        !Expect(TouchFile(second), "create second media file") ||
        !Expect(TouchFile(third), "create third media file"))
        return 1;

    const QString standardM3u = dir.filePath("standard.m3u8");
    const QString networkStream = "rtsp://example.test/live";
    if (!Expect(PlaylistFile::IsSupportedMovie(networkStream), "network stream should be accepted as media"))
        return 1;

    if (!Expect(WriteTextFile(standardM3u,
            "#EXTM3U\n"
            "#EXTINF:10,First\n"
            "001. Project Overview.mp4\n"
            "#EXTINF:20,Second\n"
            "file:///" + QDir::toNativeSeparators(second).replace("\\", "/") + "\n"
            "#EXTINF:-1,Network\n" +
            networkStream + "\n"),
            "write standard m3u"))
        return 1;

    QStringList parsed = PlaylistFile::ReadM3u(standardM3u);
    if (!Expect(parsed.size() == 3, "standard m3u should parse files and network streams") ||
        !Expect(parsed.at(0) == QFileInfo(first).canonicalFilePath(), "first standard path") ||
        !Expect(parsed.at(1) == QFileInfo(second).canonicalFilePath(), "second standard file url") ||
        !Expect(parsed.at(2) == networkStream, "network stream path"))
        return 1;

    const QString vlcM3u = dir.filePath("vlc.m3u");
    if (!Expect(WriteTextFile(vlcM3u,
            "#EXTM3U\n"
            "#EXTINF:998,001. 001.%20Project%20Overview.mp4\n"
            "#EXTINF:610,002. 002.%20Demo%20Show.mp4\n"),
            "write vlc m3u"))
        return 1;

    parsed = PlaylistFile::ReadM3u(vlcM3u);
    if (!Expect(parsed.size() == 2, "vlc extinf title fallback should parse two files") ||
        !Expect(parsed.contains(QFileInfo(first).canonicalFilePath()), "vlc first file") ||
        !Expect(parsed.contains(QFileInfo(second).canonicalFilePath()), "vlc second file"))
        return 1;

    const QString exportFile = dir.filePath("exported.m3u8");
    QString errorMessage;
    if (!Expect(PlaylistFile::WriteM3u8(exportFile, { first, third, networkStream }, &errorMessage), "export m3u8"))
    {
        std::cerr << errorMessage.toStdString() << '\n';
        return 1;
    }

    parsed = PlaylistFile::ReadM3u(exportFile);
    if (!Expect(parsed.size() == 3, "exported m3u8 should round trip files and network streams") ||
        !Expect(parsed.at(0) == QFileInfo(first).canonicalFilePath(), "export first path") ||
        !Expect(parsed.at(1) == QFileInfo(third).canonicalFilePath(), "export third path") ||
        !Expect(parsed.at(2) == networkStream, "export network stream"))
        return 1;

    return 0;
}
