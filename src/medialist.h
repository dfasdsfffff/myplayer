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

private:
    void AddFile();
    void AddFolder();
    void RemoveFile();
    void RemoveMissingFiles();
    void ClearListWithConfirm();

signals:
    void SigAddFile(QString strFileName);

private:
    QMenu m_stMenu;

    QAction m_stActAdd;
    QAction m_stActAddFolder;
    QAction m_stActRemove;
    QAction m_stActRemoveMissing;
    QAction m_stActClearList;
};
