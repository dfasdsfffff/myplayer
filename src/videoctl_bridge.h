/*
* @file 	videoctl_bridge.h
* @brief 	VideoCtl 的 Qt 信号桥接
* @note 	将 VideoCtl 的原生 Signal<> 转发为 Qt signals，
*          通过 QMetaObject::invokeMethod(Qt::QueuedConnection)
*          确保在工作线程发出的信号被编组到 Qt 主线程
*/

#pragma once

#include <QObject>
#include <QString>

class VideoCtl;

class VideoCtlBridge : public QObject
{
	Q_OBJECT

public:
	// 必须在 Qt 主线程上创建
	explicit VideoCtlBridge(QObject* parent = nullptr);
	~VideoCtlBridge() = default;

	// 连接 VideoCtl 的所有 Signal，仅调用一次
	void attach(VideoCtl* ctl);

	VideoCtlBridge(const VideoCtlBridge&) = delete;
	VideoCtlBridge& operator=(const VideoCtlBridge&) = delete;

signals:
	// Qt signals，镜像 VideoCtl 的信号，使用 Qt 类型
	void SigPlayMsg(const QString& strMsg);
	void SigFrameDimensionsChanged(int nFrameWidth, int nFrameHeight);
	void SigVideoTotalSeconds(int nSeconds);
	void SigVideoPlaySeconds(int nSeconds);
	void SigVideoVolume(double dPercent);
	void SigPauseStat(bool bPaused);
	void SigStop();
	void SigStopFinished();
	void SigStartPlay(const QString& strFileName);
	void SigPlayNextOne();
	void SigRandomPlayOne();

private:
	VideoCtl* m_ctl = nullptr;
};
