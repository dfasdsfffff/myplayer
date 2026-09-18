/*
 * @file 	displaywid.h
 * @date 	2018/01/07 11:11
 *
 * @author 	itisyang
 * @Contact	itisyang@gmail.com
 *
 * @brief 	显示控件
 * @note
 */
#pragma once

#include <QWidget>
#include <QMimeData>
#include <QDebug>
#include <QTimer>
#include <QDragEnterEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QActionGroup>
#include <QAction>
#include <SDL.h>
#include <memory>

#include "video_frame.h"

class PlaybackController;
class SubtitleRenderer;
struct SubtitleFrame;

namespace Ui {
class Show;
}

class Show : public QWidget
{
    Q_OBJECT

public:
    Show(QWidget *parent);
    ~Show();
	/**
	 * @brief	初始化
	 */
	bool Init();
    void SetPlaybackController(PlaybackController* controller);

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
    /**
     * @brief	窗口大小变化事件
     * 
     * @param	event 事件指针
     * @note
     */
    void resizeEvent(QResizeEvent *event);

    /**
     * @brief	按键事件
     * 
     * @param	
     * @return	
     * @note 	
     */
    void keyReleaseEvent(QKeyEvent *event);


    void mousePressEvent(QMouseEvent *event);
    //void contextMenuEvent(QContextMenuEvent* event);
public:
    /**
    * @brief	播放
    *
    * @param	strFile 文件名
    * @note
    */
    void OnPlay(QString strFile);
    void OnStopFinished();
    void ShowAudioOnly(const QString& location);
    void ClearAudioOnlyIndicator();

    /**
     * @brief	调整显示画面的宽高，使画面保持原比例
     *
     * @param	nFrameWidth 宽
     * @param	nFrameHeight 高
     * @return
     * @note
     */
    void OnFrameDimensionsChanged(int nFrameWidth, int nFrameHeight);
    void OnVideoFrame(std::shared_ptr<VideoFrame> frame);
    void OnSubtitleFrame(std::shared_ptr<const SubtitleFrame> frame);
    void OnVideoPlaySeconds(int seconds);
    bool LoadExternalSubtitleFile(const QString& fileName);
    bool UnloadExternalSubtitleFile();
    bool HasExternalSubtitle() const;
private:
	/**
	 * @brief	显示信息
	 * 
	 * @param	strMsg 信息内容
	 * @note 	
	 */
	void OnDisplayMsg(QString strMsg);


    void OnTimerShowCursorUpdate();

    void OnActionsTriggered(QAction *action);
private:
	/**
	 * @brief	连接信号槽	
	 */
	bool ConnectSignalSlots();


    void ChangeShow();
    bool EnsureSdlRenderer();
    void DestroySdlRenderer();
    void ClearVideoSurface();
    void RenderCurrentFrame();
signals:
    void SigOpenFile(QString strFileName);///< 增加视频文件
	void SigPlay(QString strFile); ///<播放
								   
	void SigFullScreen();//全屏播放
    void SigPlayOrPause();
    void SigStop();
    void SigShowMenu();

    void SigSeekForward();
    void SigSeekBack();
    void SigAddVolume();
    void SigSubVolume();
private:
    Ui::Show *ui;

    int m_nLastFrameWidth; ///< 记录视频宽高
    int m_nLastFrameHeight;
    std::shared_ptr<VideoFrame> m_currentFrame;
    std::unique_ptr<SubtitleRenderer> m_subtitleRenderer;
    SDL_Window* m_sdlWindow = nullptr;
    SDL_Renderer* m_sdlRenderer = nullptr;
    SDL_Texture* m_sdlTexture = nullptr;
    bool m_sdlVideoInitialized = false;
    QSize m_sdlTextureSize;
    double m_playbackSeconds = 0.0;

    QTimer timerShowCursor;

    QMenu m_stMenu;
    QActionGroup m_stActionGroup;
    PlaybackController* m_playbackController = nullptr;
    bool m_audioOnlyActive = false;
};


