#include "playlistfile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <QUrl>

bool PlaylistFile::IsNetworkStream(const QString& location)
{
    const QUrl url(location);
    const QString scheme = url.scheme().toLower();
    return scheme == "http" || scheme == "https" || scheme == "rtsp" || scheme == "rtp" || scheme == "udp";
}

bool PlaylistFile::IsSupportedMovie(const QString& fileName)
{
    return IsNetworkStream(fileName) ||
        fileName.endsWith(".mkv", Qt::CaseInsensitive) ||
        fileName.endsWith(".rmvb", Qt::CaseInsensitive) ||
        fileName.endsWith(".mp4", Qt::CaseInsensitive) ||
        fileName.endsWith(".avi", Qt::CaseInsensitive) ||
        fileName.endsWith(".flv", Qt::CaseInsensitive) ||
        fileName.endsWith(".wmv", Qt::CaseInsensitive) ||
        fileName.endsWith(".3gp", Qt::CaseInsensitive);
}

QStringList PlaylistFile::ReadM3u(const QString& playlistFileName)
{
    QStringList files;
    QFile file(playlistFileName);
    if (!file.open(QIODevice::ReadOnly))
        return files;

    const QByteArray data = file.readAll();
    QString content = QString::fromUtf8(data);
    if (content.contains(QChar::ReplacementCharacter))
        content = QString::fromLocal8Bit(data);

    const QDir playlistDir = QFileInfo(playlistFileName).dir();
    auto cleanCandidate = [](QString candidate) {
        candidate = candidate.trimmed();
        if ((candidate.startsWith('"') && candidate.endsWith('"')) ||
            (candidate.startsWith('\'') && candidate.endsWith('\'')))
            candidate = candidate.mid(1, candidate.length() - 2);
        return candidate.trimmed();
    };
    auto addResolvedFile = [&files](const QFileInfo& fileInfo) {
        if (fileInfo.exists() && fileInfo.isFile() && PlaylistFile::IsSupportedMovie(fileInfo.filePath()))
        {
            const QString canonicalPath = fileInfo.canonicalFilePath();
            if (!files.contains(canonicalPath))
                files.append(canonicalPath);
            return true;
        }

        return false;
    };
    auto addCandidate = [&files, &playlistDir, cleanCandidate, addResolvedFile](const QString& rawCandidate) {
        const QString candidate = cleanCandidate(rawCandidate);
        if (candidate.isEmpty())
            return false;
        if (IsNetworkStream(candidate))
        {
            if (!files.contains(candidate))
                files.append(candidate);
            return true;
        }

        QStringList variants;
        variants.append(candidate);

        const QString percentDecoded = QUrl::fromPercentEncoding(candidate.toUtf8());
        if (!percentDecoded.isEmpty() && percentDecoded != candidate)
            variants.append(percentDecoded);

        const QUrl url(candidate);
        if (url.isLocalFile())
            variants.append(url.toLocalFile());

        for (const QString& variant : variants)
        {
            QFileInfo fileInfo(variant);
            if (fileInfo.isRelative())
                fileInfo.setFile(playlistDir, variant);

            if (addResolvedFile(fileInfo))
                return true;
        }

        QStringList fallbackNames;
        for (const QString& variant : variants)
        {
            const QString fileName = QFileInfo(variant).fileName();
            if (fileName.isEmpty())
                continue;

            fallbackNames.append(fileName);

            QRegularExpression repeatedNumberPattern("(\\d{2,4}[.-]\\s*)");
            QRegularExpressionMatchIterator it = repeatedNumberPattern.globalMatch(fileName);
            QList<int> numberStarts;
            while (it.hasNext())
                numberStarts.append(it.next().capturedStart());
            if (numberStarts.size() >= 2)
                fallbackNames.append(fileName.mid(numberStarts.at(1)));

            const int firstSpace = fileName.indexOf(QRegularExpression("\\s+"));
            if (firstSpace >= 0)
                fallbackNames.append(fileName.mid(firstSpace + 1));
        }

        const QFileInfoList mediaFiles = playlistDir.entryInfoList(QDir::Files);
        for (const QString& fallbackName : fallbackNames)
        {
            const QString cleanName = cleanCandidate(fallbackName);
            if (cleanName.isEmpty() || !PlaylistFile::IsSupportedMovie(cleanName))
                continue;

            for (const QFileInfo& mediaFile : mediaFiles)
            {
                if (!PlaylistFile::IsSupportedMovie(mediaFile.fileName()))
                    continue;
                if (mediaFile.fileName().compare(cleanName, Qt::CaseInsensitive) == 0 ||
                    cleanName.endsWith(mediaFile.fileName(), Qt::CaseInsensitive) ||
                    mediaFile.fileName().endsWith(cleanName, Qt::CaseInsensitive))
                {
                    if (addResolvedFile(mediaFile))
                        return true;
                }
            }
        }

        return false;
    };

    QString pendingExtInfTitle;
    const QStringList lines = content.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
    for (QString line : lines)
    {
        line = line.trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith("#EXTINF:", Qt::CaseInsensitive))
        {
            if (!pendingExtInfTitle.isEmpty())
                addCandidate(pendingExtInfTitle);

            const int commaIndex = line.indexOf(',');
            pendingExtInfTitle = commaIndex >= 0 ? line.mid(commaIndex + 1).trimmed() : QString();
            continue;
        }

        if (line.startsWith('#'))
            continue;

        pendingExtInfTitle.clear();
        addCandidate(line);
    }

    if (!pendingExtInfTitle.isEmpty())
        addCandidate(pendingExtInfTitle);

    return files;
}

bool PlaylistFile::WriteM3u8(const QString& playlistFileName, const QStringList& files, QString* errorMessage)
{
    QSaveFile file(playlistFileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "#EXTM3U\n";
    for (const QString& fileName : files)
    {
        if (IsNetworkStream(fileName))
        {
            out << fileName << '\n';
            continue;
        }

        const QString canonicalPath = QFileInfo(fileName).canonicalFilePath();
        if (!canonicalPath.isEmpty())
            out << QDir::toNativeSeparators(canonicalPath) << '\n';
    }

    if (!file.commit())
    {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }

    return true;
}
