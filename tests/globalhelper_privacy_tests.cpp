#include "globalhelper.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
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
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);

    const QString configFile = QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath("player_config.ini");
    QFile::remove(configFile);

    const QString legacyDir = QDir(QCoreApplication::applicationDirPath()).filePath("config");
    const QString legacyFile = QDir(legacyDir).filePath("player_config.ini");
    QFile::remove(legacyFile);
    QDir().mkpath(legacyDir);
    {
        QSettings legacy(legacyFile, QSettings::IniFormat);
        legacy.setValue("play/volume", 0.25);
        legacy.beginWriteArray("playlist");
        legacy.setArrayIndex(0);
        legacy.setValue("movie", "https://example.test/public");
        legacy.setArrayIndex(1);
        legacy.setValue("movie", "https://example.test/secret?token=hidden");
        legacy.endArray();
        legacy.beginWriteArray("recent_files");
        legacy.setArrayIndex(0);
        legacy.setValue("file", "https://example.test/recent");
        legacy.setArrayIndex(1);
        legacy.setValue("file", "https://example.test/recent?access_token=hidden");
        legacy.endArray();
        legacy.sync();
    }

    const QString preferenceFile = GlobalHelper::PreferencesFilePath();
    if (!Expect(preferenceFile == configFile, "preferences must use AppConfigLocation") ||
        !Expect(QFile::exists(configFile), "legacy preferences must migrate to AppConfigLocation"))
        return 1;

    QSettings migrated(configFile, QSettings::IniFormat);
    if (!Expect(migrated.value("play/volume").toDouble() == 0.25, "migration must preserve legacy settings") ||
        !Expect(migrated.value("migration/version").toInt() == 1, "migration must record its version"))
        return 1;

    const int migratedPlaylistSize = migrated.beginReadArray("playlist");
    migrated.setArrayIndex(0);
    const QString migratedPlaylistLocation = migrated.value("movie").toString();
    migrated.endArray();
    const int migratedRecentSize = migrated.beginReadArray("recent_files");
    migrated.setArrayIndex(0);
    const QString migratedRecentLocation = migrated.value("file").toString();
    migrated.endArray();
    if (!Expect(migratedPlaylistSize == 1 && migratedPlaylistLocation == "https://example.test/public",
            "migration must omit sensitive playlist locations") ||
        !Expect(migratedRecentSize == 1 && migratedRecentLocation == "https://example.test/recent",
            "migration must omit sensitive recent locations"))
        return 1;

    const QString publicLocation = "https://example.test/live";
    const QString secretLocation = "https://example.test/live?token=hidden";
    GlobalHelper::SavePlaylist({ publicLocation, secretLocation });
    GlobalHelper::SavePlaylist({ publicLocation });
    GlobalHelper::SaveRecentFiles({ publicLocation, secretLocation });

    QSettings settings(configFile, QSettings::IniFormat);
    const int playlistSize = settings.beginReadArray("playlist");
    settings.setArrayIndex(0);
    const QString persistedPlaylist = settings.value("movie").toString();
    settings.endArray();
    const int recentSize = settings.beginReadArray("recent_files");
    settings.setArrayIndex(0);
    const QString persistedRecent = settings.value("file").toString();
    settings.endArray();
    if (!Expect(playlistSize == 1 && persistedPlaylist == publicLocation, "playlist must remove stale and secret entries") ||
        !Expect(recentSize == 1 && persistedRecent == publicLocation, "recent files must omit secret entries"))
        return 1;

    QFile::remove(configFile);
    QFile::remove(legacyFile);
    return 0;
}
