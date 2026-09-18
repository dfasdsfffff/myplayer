#pragma once

#include <QAction>
#include <QListWidget>
#include <QMenu>

class MediaList : public QListWidget
{
    Q_OBJECT

public:
    MediaList(QWidget *parent = 0);
    ~MediaList();
    bool Init();

protected:
    void contextMenuEvent(QContextMenuEvent* event);
    void dropEvent(QDropEvent* event) override;

private:
    void AddFile();
    void AddFolder();
    void OpenPlaylist();
    void ExportPlaylist();
    void RemoveFile();
    void RemoveMissingFiles();
    void ClearListWithConfirm();

signals:
    void SigAddFile(QString strFileName);
    void SigOpenPlaylist();
    void SigExportPlaylist();
    void SigListMutated(int preferredRow);

private:
    QMenu m_stMenu;

    QAction m_stActAdd;
    QAction m_stActAddFolder;
    QAction m_stActOpenPlaylist;
    QAction m_stActExportPlaylist;
    QAction m_stActRemove;
    QAction m_stActRemoveMissing;
    QAction m_stActClearList;
};
