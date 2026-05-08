#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <QUrl>

#include "playlist.h"
#include "ui_playlist.h"

#include "globalhelper.h"



Playlist::Playlist(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Playlist),
    m_nCurrentPlayListIndex(0)
{
    ui->setupUi(this);
	
}

Playlist::~Playlist()
{
    QStringList strListPlayList;
    for (int i = 0; i < ui->List->count(); i++)
    {
        strListPlayList.append(ui->List->item(i)->toolTip());
    }
    GlobalHelper::SavePlaylist(strListPlayList);

    delete ui;
}

bool Playlist::Init()
{
    if (ui->List->Init() == false)
    {
        return false;
    }

    if (InitUi() == false)
    {
        return false;
    }

    if (ConnectSignalSlots() == false)
    {
        return false;
    }

    setAcceptDrops(true);

	return true;
}

bool Playlist::InitUi()
{
    setStyleSheet(GlobalHelper::GetThemeStr("://res/qss/playlist.css"));
    //ui->List->hide();
    //this->setFixedWidth(ui->HideOrShowBtn->width());
    //GlobalHelper::SetIcon(ui->HideOrShowBtn, 12, QChar(0xf104));

    ui->List->clear();

    QStringList strListPlaylist;
    GlobalHelper::GetPlaylist(strListPlaylist);

    for (QString strVideoFile : strListPlaylist)
    {
        AddFileItem(strVideoFile);
    }
    if (strListPlaylist.length() > 0)
    {
        ui->List->setCurrentRow(0);
    }

    //ui->List->addItems(strListPlaylist);


    return true;
}

bool Playlist::ConnectSignalSlots()
{
	QList<bool> listRet;
	bool bRet;

    bRet = connect(ui->List, &MediaList::SigAddFile, this, &Playlist::OnAddFile);
    listRet.append(bRet);
    bRet = connect(ui->List, &MediaList::SigOpenPlaylist, this, &Playlist::OnOpenPlaylist);
    listRet.append(bRet);
    bRet = connect(ui->List, &MediaList::SigExportPlaylist, this, &Playlist::OnExportPlaylist);
    listRet.append(bRet);

	for (bool bReturn : listRet)
	{
		if (bReturn == false)
		{
			return false;
		}
	}

	return true;
}

void Playlist::on_List_itemDoubleClicked(QListWidgetItem *item)
{
	emit SigPlay(item->data(Qt::UserRole).toString());
    m_nCurrentPlayListIndex = ui->List->row(item);
    ui->List->setCurrentRow(m_nCurrentPlayListIndex);
}

bool Playlist::GetPlaylistStatus()
{
    if (this->isHidden())
    {
        return false;
    }

    return true;
}

void Playlist::GetPlaylist(QStringList& playList)
{
    playList.clear();
    for (int i = 0; i < ui->List->count(); i++)
    {
        QListWidgetItem* item = ui->List->item(i);
        playList.append(item->toolTip());
    }
}

void Playlist::OnAddFile(QString strFileName)
{
    AddFileItem(strFileName);
}

void Playlist::OnAddFileAndPlay(QString strFileName)
{
    QListWidgetItem* pItem = AddFileItem(strFileName);
    if (!pItem)
        return;

    on_List_itemDoubleClicked(pItem);
}

bool Playlist::IsSupportedMovie(const QString& strFileName) const
{
    return strFileName.endsWith(".mkv", Qt::CaseInsensitive) ||
        strFileName.endsWith(".rmvb", Qt::CaseInsensitive) ||
        strFileName.endsWith(".mp4", Qt::CaseInsensitive) ||
        strFileName.endsWith(".avi", Qt::CaseInsensitive) ||
        strFileName.endsWith(".flv", Qt::CaseInsensitive) ||
        strFileName.endsWith(".wmv", Qt::CaseInsensitive) ||
        strFileName.endsWith(".3gp", Qt::CaseInsensitive);
}

QListWidgetItem* Playlist::FindItemByPath(const QString& filePath) const
{
    const QString cleanPath = QFileInfo(filePath).canonicalFilePath();
    if (cleanPath.isEmpty())
        return nullptr;

    for (int i = 0; i < ui->List->count(); ++i)
    {
        QListWidgetItem* item = ui->List->item(i);
        const QString itemPath = QFileInfo(item->data(Qt::UserRole).toString()).canonicalFilePath();
        if (itemPath == cleanPath)
            return item;
    }

    return nullptr;
}

QListWidgetItem* Playlist::AddFileItem(const QString& strFileName)
{
    if (!IsSupportedMovie(strFileName))
        return nullptr;

    QFileInfo fileInfo(strFileName);
    if (!fileInfo.exists() || !fileInfo.isFile())
        return nullptr;

    if (QListWidgetItem* existingItem = FindItemByPath(fileInfo.filePath()))
        return existingItem;

    QListWidgetItem* pItem = new QListWidgetItem(ui->List);
    pItem->setData(Qt::UserRole, QVariant(fileInfo.canonicalFilePath()));
    pItem->setText(fileInfo.fileName());
    pItem->setToolTip(fileInfo.canonicalFilePath());
    ui->List->addItem(pItem);
    return pItem;
}

void Playlist::OnBackwardPlay()
{
    if (ui->List->count() == 0)
        return;

    if (m_nCurrentPlayListIndex == 0)
    {
        m_nCurrentPlayListIndex = ui->List->count() - 1;
        on_List_itemDoubleClicked(ui->List->item(m_nCurrentPlayListIndex));
        ui->List->setCurrentRow(m_nCurrentPlayListIndex);
    }
    else
    {
        m_nCurrentPlayListIndex--;
        on_List_itemDoubleClicked(ui->List->item(m_nCurrentPlayListIndex));
        ui->List->setCurrentRow(m_nCurrentPlayListIndex);
    }
}

void Playlist::OnForwardPlay()
{
    if (ui->List->count() == 0)
        return;

    if (m_nCurrentPlayListIndex == ui->List->count() - 1)
    {
        m_nCurrentPlayListIndex = 0;
        on_List_itemDoubleClicked(ui->List->item(m_nCurrentPlayListIndex));
        ui->List->setCurrentRow(m_nCurrentPlayListIndex);
    }
    else
    {
        m_nCurrentPlayListIndex++;
        on_List_itemDoubleClicked(ui->List->item(m_nCurrentPlayListIndex));
        ui->List->setCurrentRow(m_nCurrentPlayListIndex);
    }
}

void Playlist::OnRandomPlay()
{
    if (ui->List->count() == 0)
    {
        return;
    }
    int nRandomIndex = QRandomGenerator::global()->bounded(ui->List->count());
    on_List_itemDoubleClicked(ui->List->item(nRandomIndex));
    m_nCurrentPlayListIndex = nRandomIndex;
    ui->List->setCurrentRow(m_nCurrentPlayListIndex);
}

QStringList Playlist::ReadM3uPlaylist(const QString& playlistFileName) const
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
    auto addResolvedFile = [this, &files](const QFileInfo& fileInfo) {
        if (fileInfo.exists() && fileInfo.isFile() && IsSupportedMovie(fileInfo.filePath()))
        {
            const QString canonicalPath = fileInfo.canonicalFilePath();
            if (!files.contains(canonicalPath))
                files.append(canonicalPath);
            return true;
        }

        return false;
    };
    auto addCandidate = [this, &files, &playlistDir, cleanCandidate, addResolvedFile](const QString& rawCandidate) {
        const QString candidate = cleanCandidate(rawCandidate);
        if (candidate.isEmpty())
            return false;

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

            fallbackNames.append(fileName.mid(fileName.indexOf(QRegularExpression("\\s+")) + 1));
        }

        const QFileInfoList mediaFiles = playlistDir.entryInfoList(QDir::Files);
        for (const QString& fallbackName : fallbackNames)
        {
            const QString cleanName = cleanCandidate(fallbackName);
            if (cleanName.isEmpty() || !IsSupportedMovie(cleanName))
                continue;

            for (const QFileInfo& mediaFile : mediaFiles)
            {
                if (!IsSupportedMovie(mediaFile.fileName()))
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

void Playlist::OnOpenPlaylist()
{
    const QString playlistFileName = QFileDialog::getOpenFileName(this, "Open playlist", QDir::homePath(),
        "M3U playlists (*.m3u *.m3u8);;All files (*.*)");
    if (playlistFileName.isEmpty())
        return;

    const QStringList files = ReadM3uPlaylist(playlistFileName);
    for (const QString& fileName : files)
        AddFileItem(fileName);

    if (!files.isEmpty() && ui->List->currentRow() < 0)
        ui->List->setCurrentRow(0);
}

void Playlist::OnExportPlaylist()
{
    QStringList playList;
    GetPlaylist(playList);
    if (playList.isEmpty())
    {
        QMessageBox::information(this, "Export playlist", "The playlist is empty.");
        return;
    }

    QString playlistFileName = QFileDialog::getSaveFileName(this, "Export playlist", QDir::homePath() + "/playlist.m3u8",
        "M3U8 playlist (*.m3u8);;M3U playlist (*.m3u);;All files (*.*)");
    if (playlistFileName.isEmpty())
        return;

    if (!playlistFileName.endsWith(".m3u", Qt::CaseInsensitive) &&
        !playlistFileName.endsWith(".m3u8", Qt::CaseInsensitive))
        playlistFileName.append(".m3u8");

    QSaveFile file(playlistFileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, "Export playlist", "Failed to open playlist file for writing.");
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "#EXTM3U\n";
    for (const QString& fileName : playList)
    {
        const QString canonicalPath = QFileInfo(fileName).canonicalFilePath();
        if (!canonicalPath.isEmpty())
            out << QDir::toNativeSeparators(canonicalPath) << '\n';
    }

    if (!file.commit())
        QMessageBox::warning(this, "Export playlist", "Failed to save playlist file.");
}

void Playlist::dropEvent(QDropEvent *event)
{
    QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty())
    {
        return;
    }

    for (QUrl url : urls)
    {
        QString strFileName = url.toLocalFile();

        OnAddFile(strFileName);
    }
}

void Playlist::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
}
