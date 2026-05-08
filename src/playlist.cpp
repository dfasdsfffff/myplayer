#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>

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
