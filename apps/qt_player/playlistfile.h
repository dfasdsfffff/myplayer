#ifndef PLAYLISTFILE_H
#define PLAYLISTFILE_H

#include <QString>
#include <QStringList>

class PlaylistFile
{
public:
    static QStringList ReadM3u(const QString& playlistFileName);
    static bool WriteM3u8(const QString& playlistFileName, const QStringList& files, QString* errorMessage = nullptr);
    static bool IsSupportedMovie(const QString& fileName);
};

#endif // PLAYLISTFILE_H
