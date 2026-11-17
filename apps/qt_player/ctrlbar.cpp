/*
 * @file 	ctrlbar.cpp
 * @date 	2018/03/10 22:27
 *
 * @author 	itisyang
 * @Contact	itisyang@gmail.com
 *
 * @brief 	控制界面
 * @note
 */
#include <QDebug>
#include <QTime>
#include <QSettings>
#include <QSignalBlocker>

#include "ctrlbar.h"
#include "player_icons.h"
#include <algorithm>
#include "ui_ctrlbar.h"

#include "globalhelper.h"

CtrlBar::CtrlBar(QWidget* parent) :
	QWidget(parent),
	ui(new Ui::CtrlBar)
{
	ui->setupUi(this);

}

CtrlBar::~CtrlBar()
{
	delete ui;
}

double CtrlBar::GetSpeed() const
{
	// 从 SpeedCombo 获取当前选中的速度
	return ui->SpeedCombo->currentText().left(ui->SpeedCombo->currentText().length() - 1).toDouble();
}

bool CtrlBar::Init()
{
    setAttribute(Qt::WA_StyledBackground);
    ui->PlaylistCtrlBtn->setCheckable(true);
    ui->PlaylistCtrlBtn->setChecked(true);
    PlayerIcons::set(ui->ControlFullScreenBtn, PlayerIcons::Symbol::Fullscreen);
    ui->ControlFullScreenBtn->setToolTip("全屏 (Enter)");
	ui->SpeedCombo->setToolTip("播放速度");
	ui->SpeedCombo->addItem("0.5x");
	ui->SpeedCombo->addItem("0.8x");
	ui->SpeedCombo->addItem("1.0x");
	ui->SpeedCombo->addItem("1.5x");
	ui->SpeedCombo->addItem("2.0x");
	ui->SpeedCombo->setCurrentIndex(2); // 默认选择1.0x

	setStyleSheet(GlobalHelper::GetThemeStr("://res/qss/ctrlbar.css"));

	PlayerIcons::set(ui->PlayOrPauseBtn, PlayerIcons::Symbol::Play, true);
	PlayerIcons::set(ui->StopBtn, PlayerIcons::Symbol::Stop);
	PlayerIcons::set(ui->VolumeBtn, PlayerIcons::Symbol::Volume);
	PlayerIcons::set(ui->PlaylistCtrlBtn, PlayerIcons::Symbol::List);
	PlayerIcons::set(ui->ForwardBtn, PlayerIcons::Symbol::Next);
	PlayerIcons::set(ui->BackwardBtn, PlayerIcons::Symbol::Previous);
	PlayerIcons::set(ui->SettingBtn, PlayerIcons::Symbol::Settings);


	m_curLoopPolicy = VideoLoopPolicy::LOOP_ALL;
	UpdateLoopButton();

	ui->PlaylistCtrlBtn->setToolTip("播放列表");
	ui->SettingBtn->setToolTip("设置");
	ui->VolumeBtn->setToolTip("静音");
	ui->ForwardBtn->setToolTip("下一个");
	ui->BackwardBtn->setToolTip("上一个");
	ui->StopBtn->setToolTip("停止");
	ui->PlayOrPauseBtn->setToolTip("播放");

	ConnectSignalSlots();
	// 设置默认音量，如果之前保存过音量设置，则使用之前的设置
	double dPercent = -1.0;
	GlobalHelper::GetPlayVolume(dPercent);
	if (dPercent != -1.0)
	{
		emit SigPlayVolume(dPercent);
		OnVideopVolume(dPercent);
	}

	return true;

}

void CtrlBar::ResetSpeed()
{
	ui->SpeedCombo->setCurrentIndex(2); // 默认选择1.0x
}

void CtrlBar::ApplyPreferences(const AppPreferences& preferences)
{
    const AppPreferences sanitized = SanitizePreferences(preferences);
    const QSignalBlocker speedBlocker(ui->SpeedCombo);
    const QSignalBlocker volumeBlocker(ui->VolumeSlider);
    ui->VolumeSlider->setValue(qRound(sanitized.volume * MAX_SLIDER_VALUE));
    m_dLastVolumePercent = sanitized.volume;
    m_curLoopPolicy = sanitized.loopPolicy;
    UpdateLoopButton();
    const QString speedText = QString::number(sanitized.speed, 'f', 1) + "x";
    const int speedIndex = ui->SpeedCombo->findText(speedText);
    if (speedIndex >= 0)
        ui->SpeedCombo->setCurrentIndex(speedIndex);
    OnVideopVolume(sanitized.volume);
    emit SigPlayVolume(sanitized.volume);
    emit SigSpeedChanged(sanitized.speed);
    emit SigPlayLoopPolicyChanged(sanitized.loopPolicy);
}

bool CtrlBar::ConnectSignalSlots()
{


    connect(ui->ControlFullScreenBtn, &QPushButton::clicked, this, &CtrlBar::SigFullScreen);
	connect(ui->PlaylistCtrlBtn, &QPushButton::clicked, this, &CtrlBar::SigShowOrHidePlaylist);
	connect(ui->PlaySlider, &CustomSlider::SigCustomSliderValueChanged, this, &CtrlBar::OnPlaySliderValueChanged);
	connect(ui->VolumeSlider, &CustomSlider::SigCustomSliderValueChanged, this, &CtrlBar::OnVolumeSliderValueChanged);
	connect(ui->BackwardBtn, &QPushButton::clicked, this, &CtrlBar::SigBackwardPlay);
	connect(ui->ForwardBtn, &QPushButton::clicked, this, &CtrlBar::SigForwardPlay);
	connect(ui->SpeedCombo, &QComboBox::currentTextChanged, this, [this](const QString& text) {
		double speed = text.left(text.length() - 1).toDouble();
		emit SigSpeedChanged(speed);
		});
	connect(ui->CycleSettingBtn, &QPushButton::clicked, this, &CtrlBar::OnCycleSettingBtnClicked);
	return true;
}

void CtrlBar::OnVideoTotalSeconds(int nSeconds)
{
	m_nTotalPlaySeconds = nSeconds;

	QString timeStr = GlobalHelper::FormatTime(nSeconds);
	// 将 "HH:MM:SS" 格式转换为 QTime
	QTime totalTime = QTime::fromString(timeStr, "hh:mm:ss");
	ui->VideoTotalTimeTimeEdit->setTime(totalTime);
}


void CtrlBar::OnVideoPlaySeconds(int nSeconds)
{
	QString timeStr = GlobalHelper::FormatTime(nSeconds);
	// 将 "HH:MM:SS" 格式转换为 QTime
	QTime playTime = QTime::fromString(timeStr, "hh:mm:ss");
	ui->VideoPlayTimeTimeEdit->setTime(playTime);

	if (m_nTotalPlaySeconds > 0)
		if (!ui->PlaySlider->isSliderDown())
            ui->PlaySlider->setValue(nSeconds * 1.0 / m_nTotalPlaySeconds * MAX_SLIDER_VALUE);
}

void CtrlBar::OnVideopVolume(double dPercent)
{
    dPercent = std::clamp(dPercent, 0.0, 1.0);
	ui->VolumeSlider->setValue(dPercent * MAX_SLIDER_VALUE);
    ui->VolumeLabel->setText(QString::number(qRound(dPercent * 100)) + "%");
    if (dPercent > 0) m_unmutedVolume = dPercent;
    ui->VolumeBtn->setToolTip(dPercent == 0 ? "取消静音" : "静音");
	m_dLastVolumePercent = dPercent;

	if (m_dLastVolumePercent == 0)
	{
		PlayerIcons::set(ui->VolumeBtn, PlayerIcons::Symbol::Mute);
	}
	else
	{
		PlayerIcons::set(ui->VolumeBtn, PlayerIcons::Symbol::Volume);
	}

	GlobalHelper::SavePlayVolume(dPercent);
}

void CtrlBar::OnPauseStat(bool bPaused)
{
	if (bPaused)
	{
		PlayerIcons::set(ui->PlayOrPauseBtn, PlayerIcons::Symbol::Play, true);
		ui->PlayOrPauseBtn->setToolTip("播放");
	}
	else
	{
		PlayerIcons::set(ui->PlayOrPauseBtn, PlayerIcons::Symbol::Pause, true);
		ui->PlayOrPauseBtn->setToolTip("暂停");
	}
}

void CtrlBar::OnStopFinished()
{
	m_nTotalPlaySeconds = 0;
	ui->PlaySlider->setValue(0);
	QTime StopTime(0, 0, 0);
	ui->VideoTotalTimeTimeEdit->setTime(StopTime);
	ui->VideoPlayTimeTimeEdit->setTime(StopTime);
	PlayerIcons::set(ui->PlayOrPauseBtn, PlayerIcons::Symbol::Play, true);
	ui->PlayOrPauseBtn->setToolTip("播放");
}

void CtrlBar::SetSeekEnabled(bool enabled)
{
	ui->PlaySlider->setEnabled(enabled);
	ui->PlaySlider->setToolTip(enabled ? "拖动以定位" : "直播或不可定位媒体不支持定位");
}

void CtrlBar::OnPlaySliderValueChanged()
{
	// 计算当前播放进度百分比，并发出信号通知播放器调整播放位置
	double dPercent = ui->PlaySlider->value() * 1.0 / ui->PlaySlider->maximum();
	emit SigPlaySeek(dPercent);
}

void CtrlBar::OnVolumeSliderValueChanged()
{
	// 计算当前音量百分比，并发出信号通知播放器调整音量
	double dPercent = ui->VolumeSlider->value() * 1.0 / ui->VolumeSlider->maximum();
	emit SigPlayVolume(dPercent);

	OnVideopVolume(dPercent);
}

void CtrlBar::on_PlayOrPauseBtn_clicked()
{
	emit SigPlayOrPause();
}

void CtrlBar::on_VolumeBtn_clicked()
{
    const double volume = m_dLastVolumePercent > 0 ? 0 : m_unmutedVolume;
    OnVideopVolume(volume);
    emit SigPlayVolume(volume);
}

void CtrlBar::on_StopBtn_clicked()
{
	emit SigStop();
}

void CtrlBar::on_SettingBtn_clicked()
{
	emit SigShowSetting();
}

void CtrlBar::OnCycleSettingBtnClicked()
{
    m_curLoopPolicy = static_cast<VideoLoopPolicy>((static_cast<int>(m_curLoopPolicy) + 1) % static_cast<int>(VideoLoopPolicy::LOOP_MAX));
    UpdateLoopButton();
    emit SigPlayLoopPolicyChanged(m_curLoopPolicy);
}

void CtrlBar::UpdateLoopButton()
{
    QString text;
    switch (m_curLoopPolicy) {
    case VideoLoopPolicy::LOOP_NONE: text = QStringLiteral("不循环"); break;
    case VideoLoopPolicy::LOOP_ALL: text = QStringLiteral("列表循环"); break;
    case VideoLoopPolicy::LOOP_SINGLE: text = QStringLiteral("单首循环"); break;
    case VideoLoopPolicy::LOOP_RANDOM: text = QStringLiteral("随机播放"); break;
    default: return;
    }
    ui->CycleSettingBtn->setIcon(PlayerIcons::icon(PlayerIcons::Symbol::Repeat));
    ui->CycleSettingBtn->setIconSize(QSize(14, 14));
    ui->CycleSettingBtn->setText(text);
    ui->CycleSettingBtn->setToolTip(QStringLiteral("循环模式：") + text);
}

void CtrlBar::SetPlaylistVisible(bool visible)
{
    ui->PlaylistCtrlBtn->setChecked(visible);
}
