/*
* @file 	videoctl_bridge.cpp
* @brief 	VideoCtl 的 Qt 信号桥接实现
*/

#include "videoctl_bridge.h"
#include "videoctl.h"

VideoCtlBridge::VideoCtlBridge(QObject* parent)
	: QObject(parent)
{
}

void VideoCtlBridge::attach(VideoCtl* ctl)
{
	m_ctl = ctl;

	// 对每个 VideoCtl Signal 注册 lambda 回调，
	// 通过 QMetaObject::invokeMethod(Qt::QueuedConnection) 编组到 Qt 主线程，
	// 然后 emit 对应的 Qt signal

	ctl->SigPlayMsg.connect([this](const std::string& msg) {
		QMetaObject::invokeMethod(this, [this, msg]() {
			emit SigPlayMsg(QString::fromStdString(msg));
		}, Qt::QueuedConnection);
	});

	ctl->SigFrameDimensionsChanged.connect([this](int w, int h) {
		QMetaObject::invokeMethod(this, [this, w, h]() {
			emit SigFrameDimensionsChanged(w, h);
		}, Qt::QueuedConnection);
	});

	ctl->SigVideoTotalSeconds.connect([this](int s) {
		QMetaObject::invokeMethod(this, [this, s]() {
			emit SigVideoTotalSeconds(s);
		}, Qt::QueuedConnection);
	});

	ctl->SigVideoPlaySeconds.connect([this](int s) {
		QMetaObject::invokeMethod(this, [this, s]() {
			emit SigVideoPlaySeconds(s);
		}, Qt::QueuedConnection);
	});

	ctl->SigVideoVolume.connect([this](double v) {
		QMetaObject::invokeMethod(this, [this, v]() {
			emit SigVideoVolume(v);
		}, Qt::QueuedConnection);
	});

	ctl->SigPauseStat.connect([this](bool b) {
		QMetaObject::invokeMethod(this, [this, b]() {
			emit SigPauseStat(b);
		}, Qt::QueuedConnection);
	});

	ctl->SigStop.connect([this]() {
		QMetaObject::invokeMethod(this, [this]() {
			emit SigStop();
		}, Qt::QueuedConnection);
	});

	ctl->SigStopFinished.connect([this]() {
		QMetaObject::invokeMethod(this, [this]() {
			emit SigStopFinished();
		}, Qt::QueuedConnection);
	});

	ctl->SigStartPlay.connect([this](const std::string& f) {
		QMetaObject::invokeMethod(this, [this, f]() {
			emit SigStartPlay(QString::fromStdString(f));
		}, Qt::QueuedConnection);
	});

	ctl->SigPlayNextOne.connect([this]() {
		QMetaObject::invokeMethod(this, [this]() {
			emit SigPlayNextOne();
		}, Qt::QueuedConnection);
	});

	ctl->SigRandomPlayOne.connect([this]() {
		QMetaObject::invokeMethod(this, [this]() {
			emit SigRandomPlayOne();
		}, Qt::QueuedConnection);
	});
}
