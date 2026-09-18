/*
 * @file 	playlist.h
 * @date 	2018/01/07 11:12
 *
 * @author 	itisyang
 * @Contact	itisyang@gmail.com
 *
 * @brief 	播放列表控件
 * @note
 */
#pragma once

#include <QWidget>
#include <QListWidgetItem>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QMimeData>

namespace Ui {
class Playlist;
}

class Playlist : public QWidget
{
    Q_OBJECT

public:
    explicit Playlist(QWidget *parent = 0);
    ~Playlist();

	bool Init();


	/**
	 * @brief	获取播放列表状态
	 * 
	 * @return	true 显示 false 隐藏
	 * @note 	
	 */
    bool GetPlaylistStatus();

	/**
	 * @brief	获取播放列表文件路径
	 * 
	 * @param	playList 输出参数，播放列表文件路径
	 * @note 	
	 */
	void GetPlaylist(QStringList& playList);
public:
	/**
	 * @brief	添加文件
	 * 
	 * @param	strFileName 文件完整路径
	 * @note 	
	 */
    void OnAddFile(QString strFileName);
    void OnAddFileAndPlay(QString strFileName);

    void OnBackwardPlay();
    void OnForwardPlay();
	void OnRandomPlay();

    QString currentLocation() const;
    int rowForLocation(const QString& location) const;
    QString adjacentLocation(int direction) const;

    /* 在这里定义dock的初始大小 */
    QSize sizeHint() const
    {
        return QSize(150, 900);
    }
protected:
    /**
    * @brief	放下事件
    *
    * @param	event 事件指针
    * @note
    */
    void dropEvent(QDropEvent *event);
    /**
    * @brief	拖动事件
    *
    * @param	event 事件指针
    * @note
    */
    void dragEnterEvent(QDragEnterEvent *event);

signals:
    void SigUpdateUi();	//< 界面排布更新
	void SigPlay(QString strFile); //< 播放文件

private:
    bool InitUi();
    bool ConnectSignalSlots();
    bool IsNetworkMediaLocation(const QString& location) const;
    bool IsSupportedMovie(const QString& strFileName) const;
    QListWidgetItem* FindItemByLocation(const QString& location) const;
    QListWidgetItem* AddFileItem(const QString& strFileName);
    
private slots:

	void on_List_itemDoubleClicked(QListWidgetItem *item);
    void OnOpenPlaylist();
    void OnExportPlaylist();
    void OnListMutated(int preferredRow);

private:
    Ui::Playlist *ui;

    QString m_currentLocation;
};


