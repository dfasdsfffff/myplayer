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

VideoCtlBridge::~VideoCtlBridge()
{
	detach();
}

void VideoCtlBridge::detach()
{
	m_connections.clear();
	m_ctl = nullptr;
}

void VideoCtlBridge::attach(VideoCtl* ctl)
{
	detach();

	if (!ctl)
		return;

	m_ctl = ctl;
	QPointer<VideoCtlBridge> self(this);

	m_connections.emplace_back(ctl->SigPlayMsg.connect([self](const std::string& msg) {
		if (!self)
			return;
		QMetaObject::invokeMethod(self.data(), [self, msg]() {
			if (auto bridge = self.data())
				emit bridge->SigPlayMsg(QString::fromStdString(msg));
		}, Qt::QueuedConnection);
	}));

	m_connections.emplace_back(ctl->SigFrameDimensionsChanged.connect([self](int w, int h) {
		if (!self)
			return;
		QMetaObject::invokeMethod(self.data(), [self, w, h]() {
			if (auto bridge = self.data())
				emit bridge->SigFrameDimensionsChanged(w, h);
		}, Qt::QueuedConnection);
	}));

	m_connections.emplace_back(ctl->SigVideoTotalSeconds.connect([self](int s) {
		if (!self)
			return;
		QMetaObject::invokeMethod(self.data(), [self, s]() {
			if (auto bridge = self.data())
				emit bridge->SigVideoTotalSeconds(s);
		}, Qt::QueuedConnection);
	}));

	m_connections.emplace_back(ctl->SigVideoPlaySeconds.connect([self](int s) {
		if (!self)
			return;
		QMetaObject::invokeMethod(self.data(), [self, s]() {
			if (auto bridge = self.data())
				emit bridge->SigVideoPlaySeconds(s);
		}, Qt::QueuedConnection);
	}));

	m_connections.emplace_back(ctl->SigVideoVolume.connect([self](double v) {
		if (!self)
			return;
		QMetaObject::invokeMethod(self.data(), [self, v]() {
			if (auto bridge = self.data())
				emit bridge->SigVideoVolume(v);
		}, Qt::QueuedConnection);
	}));

	m_connections.emplace_back(ctl->SigPauseStat.connect([self](bool b) {
		if (!self)
			return;
		QMetaObject::invokeMethod(self.data(), [self, b]() {
			if (auto bridge = self.data())
				emit bridge->SigPauseStat(b);
		}, Qt::QueuedConnection);
	}));

	m_connections.emplace_back(ctl->SigStop.connect([self]() {
		if (!self)
			return;
		QMetaObject::invokeMethod(self.data(), [self]() {
			if (auto bridge = self.data())
				emit bridge->SigStop();
		}, Qt::QueuedConnection);
	}));

	m_connections.emplace_back(ctl->SigStopFinished.connect([self]() {
		if (!self)
			return;
		QMetaObject::invokeMethod(self.data(), [self]() {
			if (auto bridge = self.data())
				emit bridge->SigStopFinished();
		}, Qt::QueuedConnection);
	}));

	m_connections.emplace_back(ctl->SigStartPlay.connect([self](const std::string& f) {
		if (!self)
			return;
		QMetaObject::invokeMethod(self.data(), [self, f]() {
			if (auto bridge = self.data())
				emit bridge->SigStartPlay(QString::fromStdString(f));
		}, Qt::QueuedConnection);
	}));

	m_connections.emplace_back(ctl->SigPlayNextOne.connect([self]() {
		if (!self)
			return;
		QMetaObject::invokeMethod(self.data(), [self]() {
			if (auto bridge = self.data())
				emit bridge->SigPlayNextOne();
		}, Qt::QueuedConnection);
	}));

	m_connections.emplace_back(ctl->SigRandomPlayOne.connect([self]() {
		if (!self)
			return;
		QMetaObject::invokeMethod(self.data(), [self]() {
			if (auto bridge = self.data())
				emit bridge->SigRandomPlayOne();
		}, Qt::QueuedConnection);
	}));
}
