/*
 * @file 	mainwid.h
 * @date 	2018/01/07 11:12
 *
 * @author 	itisyang
 * @Contact	itisyang@gmail.com
 *
 * @brief 	主界面
 * @note
 */
#pragma once

#include <QWidget>
#include <QMouseEvent>
#include <QMenu>
#include <QAction>
#include <QPropertyAnimation>
#include <QTimer>
#include <QMainWindow>
#include <memory>

#include "playlist.h"
#include "title.h"
#include "settingwid.h"
#include "playback_runtime_bridge.h"

namespace Ui {
class MainWid;
}

class PlaybackController;
class PlaybackRuntime;

class MainWid : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWid(QMainWindow *parent = nullptr);
    ~MainWid();

    //初始化
    bool Init();
protected:
    //绘制
    void paintEvent(QPaintEvent *event);

    void enterEvent(QEvent *event);
    void leaveEvent(QEvent *event);

    //按键事件
    void keyReleaseEvent(QKeyEvent *event);

    void mousePressEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);

    void contextMenuEvent(QContextMenuEvent* event);

    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

    // 全屏模式事件过滤器（替代定时器轮询）
    bool eventFilter(QObject* watched, QEvent* event) override;


private:
    //连接信号槽
    bool ConnectSignalSlots();

    //关闭、最小化、最大化按钮响应
    void OnCloseBtnClicked();
    void OnMinBtnClicked();
    void OnMaxBtnClicked();
    //显示、隐藏播放列表
    void OnShowOrHidePlaylist();

    void OnSpeedChanged(double speed);

    /**
    * @brief	全屏播放
    */
    void OnFullScreenPlay();

    void OnCtrlBarAnimationTimeOut();

    void OnCtrlBarHideTimeOut();
    void OnShowMenu();
    void OnShowAbout();
    void OpenFile();
    void OpenNetworkStream();
    void OpenSubtitleFile();
    void UnloadSubtitleFile();
    void OnPlayFile(QString strFileName);
    void OnOpenRecentFile();
    void OnClearRecentFiles();
    void OnCycleAudioTrack();
    void OnCycleSubtitleTrack();
    void OnVideoPlaySeconds(int seconds);
    void OnPlaybackStatus(PlaybackStatus status);
    void OnMediaInfo(MediaInfo info);

    void OnShowSettingWid();


    //添加菜单
    void InitMenu();
    void MenuJsonParser(QJsonObject& json_obj, QMenu* menu);
    QMenu* AddMenuFun(QString menu_title, QMenu* menu);
    void AddActionFun(QString action_title, QMenu* menu, void(MainWid::* slot_addr)());
    void ConnectMenuAction(QAction* action, const QString& actionText, const QString& functionName, const QString& hotKey);
    void AddRecentFile(const QString& strFileName);
    void RefreshRecentFilesMenu();
    void MarkPlaybackPositionDirty();
    void FlushPlaybackPosition();
    void ApplyPreferences(const AppPreferences& preferences);

signals:
    //最大化信号
    void SigShowMax(bool bIfMax);
    void SigSeekForward();
    void SigSeekBack();
    void SigAddVolume();
    void SigSubVolume();
    void SigPlayOrPause();
    void SigOpenFile(QString strFilename);
private:
    Ui::MainWid *ui;

    const int m_nShadowWidth; ///< 阴影宽度

    bool m_bFullScreenPlay; ///< 全屏播放标志

    QPropertyAnimation *m_stCtrlbarAnimationShow = nullptr; //全屏时控制面板浮动显示
    QPropertyAnimation *m_stCtrlbarAnimationHide = nullptr; //全屏时控制面板浮动显示
    QRect m_stCtrlBarAnimationShow;//控制面板显示区域
    QRect m_stCtrlBarAnimationHide;//控制面板隐藏区域

    QTimer m_stCtrlBarAnimationTimer;
    bool m_bFullscreenCtrlBarShow = false;
    QTimer* m_stCtrlBarHideTimer;

    Playlist m_stPlaylist;
    Title m_stTitle;

    bool m_bMoveDrag;//移动窗口标志
    QPoint m_DragPosition;

    About m_stAboutWidget;
    SettingWid m_stSettingWid;
    AppPreferences m_preferences;

    QMenu m_stMenu;
    QMenu* m_pRecentFilesMenu = nullptr;
    QMenu* m_pAudioTracksMenu = nullptr;
    QMenu* m_pSubtitleTracksMenu = nullptr;
    QAction m_stActFullscreen;
    QString m_currentPlayFile;
    int m_currentPlaySeconds = 0;
    QString m_pendingPlaybackFile;
    int m_pendingPlaybackSeconds = 0;
    QString m_pendingResumeFile;
    int m_pendingResumeSeconds = 0;
    int m_currentDurationSeconds = 0;
    bool m_playbackPositionDirty = false;
    quint64 m_playbackGeneration = 0;
    quint64 m_lastFinalErrorGeneration = 0;
    QTimer m_playbackPositionSaveTimer;
    bool m_bResizeDrag = false;
    bool m_bResizeCursorOverridden = false;
    int m_resizeEdges = 0;
    QPoint m_resizeStartGlobalPos;
    QRect m_resizeStartGeometry;

    PlaybackRuntimeBridge* m_pPlaybackRuntimeBridge = nullptr;
    std::unique_ptr<PlaybackRuntime> m_playbackRuntime;
    PlaybackController* m_playbackController = nullptr;
};
