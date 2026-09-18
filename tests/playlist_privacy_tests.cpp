#include "globalhelper.h"
#include "playlist.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <iostream>

namespace {
bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    const QString configFile = QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath("player_config.ini");
    QFile::remove(configFile);

    const QString sensitiveLocation = "rtsp://alice:secret@example.test/live?token=hidden";
    QStringList locations;
    {
        Playlist playlist;
        if (!Expect(playlist.Init(), "playlist initializes"))
            return 1;
        playlist.OnAddFile(sensitiveLocation);
        playlist.GetPlaylist(locations);
        if (!Expect(locations == QStringList{sensitiveLocation}, "playlist must retain raw user-role location in memory"))
            return 1;
    }

    QStringList persisted;
    GlobalHelper::GetPlaylist(persisted);
    if (!Expect(persisted.isEmpty(), "playlist persistence must omit sensitive raw location"))
        return 1;

    QFile::remove(configFile);
    return 0;
}
