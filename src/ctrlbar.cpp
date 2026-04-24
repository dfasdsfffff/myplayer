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

#include "ctrlbar.h"
#include "ui_ctrlbar.h"

#include "globalhelper.h"

CtrlBar::CtrlBar(QWidget* parent) :
	QWidget(parent),
	ui(new Ui::CtrlBar)
{
	ui->setupUi(this);

	m_dLastVolumePercent = 1.0;
}

CtrlBar::~CtrlBar()
{
	delete ui;
}

bool CtrlBar::Init()
{
	ui->SpeedCombo->setToolTip("播放速度");
	ui->SpeedCombo->addItem("0.5x");
	ui->SpeedCombo->addItem("0.8x");
	ui->SpeedCombo->addItem("1.0x");
	ui->SpeedCombo->addItem("1.5x");
	ui->SpeedCombo->addItem("2.0x");
	ui->SpeedCombo->setCurrentIndex(2); // 默认选择1.0x

	setStyleSheet(GlobalHelper::GetQssStr("://res/qss/ctrlbar.css"));

	GlobalHelper::SetIcon(ui->PlayOrPauseBtn, 12, QChar(0xf04b));
	GlobalHelper::SetIcon(ui->StopBtn, 12, QChar(0xf04d));
	GlobalHelper::SetIcon(ui->VolumeBtn, 12, QChar(0xf028));
	GlobalHelper::SetIcon(ui->PlaylistCtrlBtn, 12, QChar(0xf036));
	GlobalHelper::SetIcon(ui->ForwardBtn, 12, QChar(0xf051));
	GlobalHelper::SetIcon(ui->BackwardBtn, 12, QChar(0xf048));
	GlobalHelper::SetIcon(ui->SettingBtn, 12, QChar(0xf013));
	GlobalHelper::SetIcon(ui->CycleSettingBtn, 20, QIcon(":/res/sequentia-play.svg"));

	m_curLoopPolicy = VideoLoopPolicy::LOOP_ALL;

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

bool CtrlBar::ConnectSignalSlots()
{
	QList<bool> listRet;
	bool bRet;


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

	int thh, tmm, tss;
	thh = nSeconds / 3600;
	tmm = (nSeconds % 3600) / 60;
	tss = (nSeconds % 60);
	QTime TotalTime(thh, tmm, tss);

	ui->VideoTotalTimeTimeEdit->setTime(TotalTime);
}


void CtrlBar::OnVideoPlaySeconds(int nSeconds)
{
	int thh, tmm, tss;
	thh = nSeconds / 3600;
	tmm = (nSeconds % 3600) / 60;
	tss = (nSeconds % 60);
	QTime TotalTime(thh, tmm, tss);

	ui->VideoPlayTimeTimeEdit->setTime(TotalTime);

	ui->PlaySlider->setValue(nSeconds * 1.0 / m_nTotalPlaySeconds * MAX_SLIDER_VALUE);
}

void CtrlBar::OnVideopVolume(double dPercent)
{
	ui->VolumeSlider->setValue(dPercent * MAX_SLIDER_VALUE);
	m_dLastVolumePercent = dPercent;

	if (m_dLastVolumePercent == 0)
	{
		GlobalHelper::SetIcon(ui->VolumeBtn, 12, QChar(0xf026));
	}
	else
	{
		GlobalHelper::SetIcon(ui->VolumeBtn, 12, QChar(0xf028));
	}

	GlobalHelper::SavePlayVolume(dPercent);
}

void CtrlBar::OnPauseStat(bool bPaused)
{
	qDebug() << "CtrlBar::OnPauseStat" << bPaused;
	if (bPaused)
	{
		GlobalHelper::SetIcon(ui->PlayOrPauseBtn, 12, QChar(0xf04b));
		ui->PlayOrPauseBtn->setToolTip("播放");
	}
	else
	{
		GlobalHelper::SetIcon(ui->PlayOrPauseBtn, 12, QChar(0xf04c));
		ui->PlayOrPauseBtn->setToolTip("暂停");
	}
}

void CtrlBar::OnStopFinished()
{
	ui->PlaySlider->setValue(0);
	QTime StopTime(0, 0, 0);
	ui->VideoTotalTimeTimeEdit->setTime(StopTime);
	ui->VideoPlayTimeTimeEdit->setTime(StopTime);
	GlobalHelper::SetIcon(ui->PlayOrPauseBtn, 12, QChar(0xf04b));
	ui->PlayOrPauseBtn->setToolTip("播放");
}

void CtrlBar::OnPlaySliderValueChanged()
{
	// 计算当前播放进度百分比，并发出信号通知播放器调整播放位置
	double dPercent = ui->PlaySlider->value() * 1.0 / ui->PlaySlider->maximum();
	emit SigPlaySeek(dPercent);
	qDebug() << dPercent;
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
	if (ui->VolumeBtn->text() == QChar(0xf028))
	{
		GlobalHelper::SetIcon(ui->VolumeBtn, 12, QChar(0xf026));
		ui->VolumeSlider->setValue(0);
		emit SigPlayVolume(0);
	}
	else
	{
		GlobalHelper::SetIcon(ui->VolumeBtn, 12, QChar(0xf028));
		ui->VolumeSlider->setValue(m_dLastVolumePercent * MAX_SLIDER_VALUE);
		emit SigPlayVolume(m_dLastVolumePercent);
	}

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
	switch (m_curLoopPolicy)
	{
	case VideoLoopPolicy::LOOP_NONE:
		GlobalHelper::SetIcon(ui->CycleSettingBtn, 20, QIcon(":/res/non-repeating-play.svg"));
		ui->CycleSettingBtn->setToolTip("循环模式：不循环");
		break;
	case VideoLoopPolicy::LOOP_ALL:
		GlobalHelper::SetIcon(ui->CycleSettingBtn, 20, QIcon(":/res/sequentia-play.svg"));
		ui->CycleSettingBtn->setToolTip("循环模式：列表循环");
		break;
	case VideoLoopPolicy::LOOP_SINGLE:
		GlobalHelper::SetIcon(ui->CycleSettingBtn, 20, QIcon(":/res/single-play.svg"));
		ui->CycleSettingBtn->setToolTip("循环模式：单首循环");
		break;
	case VideoLoopPolicy::LOOP_RANDOM:
		GlobalHelper::SetIcon(ui->CycleSettingBtn, 20, QIcon(":/res/shuffle-play.svg"));
		ui->CycleSettingBtn->setToolTip("循环模式：单首循环");
		break;
	}
	emit SigPlayLoopPolicyChanged(m_curLoopPolicy);
}
