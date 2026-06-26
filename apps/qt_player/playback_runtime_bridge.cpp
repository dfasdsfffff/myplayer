#include "playback_runtime_bridge.h"

#include "playback_runtime.h"

PlaybackRuntimeBridge::PlaybackRuntimeBridge(QObject* parent)
    : QObject(parent)
{
}

PlaybackRuntimeBridge::~PlaybackRuntimeBridge()
{
    detach();
}

void PlaybackRuntimeBridge::detach()
{
    m_connections.clear();
    m_runtime = nullptr;
}

void PlaybackRuntimeBridge::attach(PlaybackRuntime* runtime)
{
    detach();

    if (!runtime)
        return;

    m_runtime = runtime;
    QPointer<PlaybackRuntimeBridge> self(this);

    m_connections.emplace_back(runtime->SigPlayMsg.connect([self](const std::string& msg) {
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, msg]() {
            if (auto bridge = self.data())
                emit bridge->SigPlayMsg(QString::fromStdString(msg));
        }, Qt::QueuedConnection);
    }));

    m_connections.emplace_back(runtime->SigFrameDimensionsChanged.connect([self](int w, int h) {
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, w, h]() {
            if (auto bridge = self.data())
                emit bridge->SigFrameDimensionsChanged(w, h);
        }, Qt::QueuedConnection);
    }));

    m_connections.emplace_back(runtime->SigVideoFrame.connect([self](std::shared_ptr<VideoFrame> frame) {
        if (!self || !frame || frame->bgra.empty())
            return;
        QMetaObject::invokeMethod(self.data(), [self, frame]() {
            if (auto bridge = self.data())
                emit bridge->SigVideoFrame(frame);
        }, Qt::QueuedConnection);
    }));

    m_connections.emplace_back(runtime->SigVideoTotalSeconds.connect([self](int s) {
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, s]() {
            if (auto bridge = self.data())
                emit bridge->SigVideoTotalSeconds(s);
        }, Qt::QueuedConnection);
    }));

    m_connections.emplace_back(runtime->SigVideoPlaySeconds.connect([self](int s) {
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, s]() {
            if (auto bridge = self.data())
                emit bridge->SigVideoPlaySeconds(s);
        }, Qt::QueuedConnection);
    }));

    m_connections.emplace_back(runtime->SigVideoVolume.connect([self](double v) {
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, v]() {
            if (auto bridge = self.data())
                emit bridge->SigVideoVolume(v);
        }, Qt::QueuedConnection);
    }));

    m_connections.emplace_back(runtime->SigPauseStat.connect([self](bool b) {
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, b]() {
            if (auto bridge = self.data())
                emit bridge->SigPauseStat(b);
        }, Qt::QueuedConnection);
    }));

    m_connections.emplace_back(runtime->SigStop.connect([self]() {
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self]() {
            if (auto bridge = self.data())
                emit bridge->SigStop();
        }, Qt::QueuedConnection);
    }));

    m_connections.emplace_back(runtime->SigStopFinished.connect([self]() {
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self]() {
            if (auto bridge = self.data())
                emit bridge->SigStopFinished();
        }, Qt::QueuedConnection);
    }));

    m_connections.emplace_back(runtime->SigStartPlay.connect([self](const std::string& f) {
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, f]() {
            if (auto bridge = self.data())
                emit bridge->SigStartPlay(QString::fromStdString(f));
        }, Qt::QueuedConnection);
    }));

    m_connections.emplace_back(runtime->SigPlayNextOne.connect([self]() {
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self]() {
            if (auto bridge = self.data())
                emit bridge->SigPlayNextOne();
        }, Qt::QueuedConnection);
    }));

    m_connections.emplace_back(runtime->SigRandomPlayOne.connect([self]() {
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self]() {
            if (auto bridge = self.data())
                emit bridge->SigRandomPlayOne();
        }, Qt::QueuedConnection);
    }));
}
