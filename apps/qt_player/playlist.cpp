#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QUrl>

#include "playlist.h"
#include "playlistfile.h"
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
    return PlaylistFile::IsSupportedMovie(strFileName);
}

bool Playlist::IsNetworkMediaLocation(const QString& location) const
{
    return PlaylistFile::IsNetworkStream(location);
}

QListWidgetItem* Playlist::FindItemByLocation(const QString& location) const
{
    const bool networkLocation = IsNetworkMediaLocation(location);
    const QString cleanLocation = networkLocation ? location : QFileInfo(location).canonicalFilePath();
    if (cleanLocation.isEmpty())
        return nullptr;

    for (int i = 0; i < ui->List->count(); ++i)
    {
        QListWidgetItem* item = ui->List->item(i);
        const QString itemLocation = item->data(Qt::UserRole).toString();
        const QString cleanItemLocation = networkLocation ? itemLocation : QFileInfo(itemLocation).canonicalFilePath();
        if (cleanItemLocation == cleanLocation)
            return item;
    }

    return nullptr;
}

QListWidgetItem* Playlist::AddFileItem(const QString& strFileName)
{
    if (!IsSupportedMovie(strFileName))
        return nullptr;

    if (IsNetworkMediaLocation(strFileName))
    {
        if (QListWidgetItem* existingItem = FindItemByLocation(strFileName))
            return existingItem;

        const QUrl url(strFileName);
        QListWidgetItem* pItem = new QListWidgetItem(ui->List);
        pItem->setData(Qt::UserRole, QVariant(strFileName));
        pItem->setText(url.toDisplayString(QUrl::RemoveUserInfo));
        pItem->setToolTip(strFileName);
        ui->List->addItem(pItem);
        return pItem;
    }

    QFileInfo fileInfo(strFileName);
    if (!fileInfo.exists() || !fileInfo.isFile())
        return nullptr;

    if (QListWidgetItem* existingItem = FindItemByLocation(fileInfo.filePath()))
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

void Playlist::OnOpenPlaylist()
{
    const QString playlistFileName = QFileDialog::getOpenFileName(this, "Open playlist", QDir::homePath(),
        "M3U playlists (*.m3u *.m3u8);;All files (*.*)");
    if (playlistFileName.isEmpty())
        return;

    const QStringList files = PlaylistFile::ReadM3u(playlistFileName);
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

    QString errorMessage;
    if (!PlaylistFile::WriteM3u8(playlistFileName, playList, &errorMessage))
        QMessageBox::warning(this, "Export playlist", QString("Failed to save playlist file.\n%1").arg(errorMessage));
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
