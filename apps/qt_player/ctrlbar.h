/*
 * @file 	ctrlbar.h
 * @date 	2018/01/07 10:46
 *
 * @author 	itisyang
 * @Contact	itisyang@gmail.com
 *
 * @brief 	控制面板界面
 * @note
 */
#pragma once

#include "enums.h"
#include "app_preferences.h"
#include <QWidget>

namespace Ui {
class CtrlBar;
}

class CtrlBar : public QWidget
{
    Q_OBJECT

public:
    explicit CtrlBar(QWidget *parent = 0);
    ~CtrlBar();
	/**
	 * @brief	初始化UI
	 * 
	 * @return	true 成功 false 失败
	 * @note 	
	 */
    bool Init();
    void ResetSpeed();
    void ApplyPreferences(const AppPreferences& preferences);

    // 新增：获取当前状态（用于配置持久化）
    double GetVolume() const { return m_dLastVolumePercent; }
    int GetLoopPolicy() const { return static_cast<int>(m_curLoopPolicy); }
    double GetSpeed() const;

public:
    void OnVideoTotalSeconds(int nSeconds);
    void OnVideoPlaySeconds(int nSeconds);
    void OnVideopVolume(double dPercent);
    void OnPauseStat(bool bPaused);
    void OnStopFinished();
    void SetSeekEnabled(bool enabled);
private:
    void OnPlaySliderValueChanged();
    void OnVolumeSliderValueChanged();
private slots:
    void on_PlayOrPauseBtn_clicked();
    void on_VolumeBtn_clicked();
    void on_StopBtn_clicked();
    void on_SettingBtn_clicked();
	void OnCycleSettingBtnClicked();

    /**
    * @brief	连接信号槽
    *
    * @param
    * @return
    * @note
    */
    bool ConnectSignalSlots();
signals:
    void SigShowOrHidePlaylist();	//< 显示或隐藏信号
    void SigPlaySeek(double dPercent); ///< 调整播放进度
    void SigPlayVolume(double dPercent);
    void SigPlayOrPause();
    void SigStop();
    void SigForwardPlay();
    void SigBackwardPlay();
    void SigShowMenu();
    void SigShowSetting();
    void SigSpeedChanged(double speed);
	void SigPlayLoopPolicyChanged(VideoLoopPolicy policy);

private:
    Ui::CtrlBar *ui;
	VideoLoopPolicy m_curLoopPolicy = VideoLoopPolicy::LOOP_NONE;
    int m_nTotalPlaySeconds = 0;
    double m_dLastVolumePercent = 1.0;
};

