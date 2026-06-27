#include <QAbstractItemView>
#include <QContextMenuEvent>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

#include "medialist.h"
#include "playlistfile.h"

namespace {
bool IsSupportedMovieFile(const QString& fileName)
{
    return fileName.endsWith(".mkv", Qt::CaseInsensitive) ||
        fileName.endsWith(".rmvb", Qt::CaseInsensitive) ||
        fileName.endsWith(".mp4", Qt::CaseInsensitive) ||
        fileName.endsWith(".avi", Qt::CaseInsensitive) ||
        fileName.endsWith(".flv", Qt::CaseInsensitive) ||
        fileName.endsWith(".wmv", Qt::CaseInsensitive) ||
        fileName.endsWith(".3gp", Qt::CaseInsensitive);
}
}

MediaList::MediaList(QWidget *parent)
    : QListWidget(parent),
      m_stMenu(this),
      m_stActAdd(this),
      m_stActAddFolder(this),
      m_stActOpenPlaylist(this),
      m_stActExportPlaylist(this),
      m_stActRemove(this),
      m_stActRemoveMissing(this),
      m_stActClearList(this)
{
}

MediaList::~MediaList()
{
}

bool MediaList::Init()
{
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setSelectionMode(QAbstractItemView::ExtendedSelection);

    m_stActAdd.setText("添加");
    m_stMenu.addAction(&m_stActAdd);

    m_stActAddFolder.setText("添加文件夹");
    m_stMenu.addAction(&m_stActAddFolder);

    m_stActOpenPlaylist.setText("Open playlist...");
    m_stMenu.addAction(&m_stActOpenPlaylist);

    m_stActExportPlaylist.setText("Export playlist...");
    m_stMenu.addAction(&m_stActExportPlaylist);

    QMenu* stRemoveMenu = m_stMenu.addMenu("移除");
    m_stActRemove.setText("移除选择项");
    stRemoveMenu->addAction(&m_stActRemove);
    m_stActRemoveMissing.setText("移除不存在的文件");
    stRemoveMenu->addAction(&m_stActRemoveMissing);

    m_stActClearList.setText("清空列表");
    m_stMenu.addAction(&m_stActClearList);

    connect(&m_stActAdd, &QAction::triggered, this, &MediaList::AddFile);
    connect(&m_stActAddFolder, &QAction::triggered, this, &MediaList::AddFolder);
    connect(&m_stActOpenPlaylist, &QAction::triggered, this, &MediaList::OpenPlaylist);
    connect(&m_stActExportPlaylist, &QAction::triggered, this, &MediaList::ExportPlaylist);
    connect(&m_stActRemove, &QAction::triggered, this, &MediaList::RemoveFile);
    connect(&m_stActRemoveMissing, &QAction::triggered, this, &MediaList::RemoveMissingFiles);
    connect(&m_stActClearList, &QAction::triggered, this, &MediaList::ClearListWithConfirm);

    return true;
}

void MediaList::contextMenuEvent(QContextMenuEvent* event)
{
    m_stMenu.popup(event->globalPos());
    event->accept();
}

void MediaList::AddFile()
{
    QStringList listFileName = QFileDialog::getOpenFileNames(this, "Open files", QDir::homePath(),
        "Video files (*.mkv *.rmvb *.mp4 *.avi *.flv *.wmv *.3gp)");

    for (const QString& strFileName : listFileName)
        emit SigAddFile(strFileName);
}

void MediaList::AddFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, "Select folder", QDir::homePath());
    if (folder.isEmpty())
        return;

    QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString filePath = it.next();
        if (IsSupportedMovieFile(filePath))
            emit SigAddFile(filePath);
    }
}

void MediaList::OpenPlaylist()
{
    emit SigOpenPlaylist();
}

void MediaList::ExportPlaylist()
{
    emit SigExportPlaylist();
}

void MediaList::RemoveFile()
{
    const QList<QListWidgetItem*> selected = selectedItems();
    if (selected.isEmpty())
    {
        delete takeItem(currentRow());
        return;
    }

    for (QListWidgetItem* item : selected)
        delete takeItem(row(item));
}

void MediaList::RemoveMissingFiles()
{
    for (int i = count() - 1; i >= 0; --i)
    {
        QListWidgetItem* item = this->item(i);
        const QString location = item->data(Qt::UserRole).toString();
        if (!PlaylistFile::IsNetworkStream(location) && !QFileInfo::exists(location))
            delete takeItem(i);
    }
}

void MediaList::ClearListWithConfirm()
{
    if (count() == 0)
        return;

    const auto result = QMessageBox::question(this, "Clear list", "Clear the playlist?");
    if (result == QMessageBox::Yes)
        clear();
}
