#include "playback_runtime_bridge.h"

#include "playback_runtime.h"

#include <QDebug>
#include <QMetaObject>

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
    const uint64_t published = m_publishedVideoFrames.load(std::memory_order_relaxed);
    if (published != 0) {
        qDebug() << "Video frame delivery: published" << published
                 << "presented" << m_presentedVideoFrames.load(std::memory_order_relaxed)
                 << "coalesced" << m_videoFrameMailbox.coalescedFrames();
    }
    m_videoFrameGeneration.fetch_add(1, std::memory_order_acq_rel);
    m_videoFrameMailbox.clear();
    m_connections.clear();
    m_runtime = nullptr;
}

void PlaybackRuntimeBridge::scheduleVideoFrameDelivery()
{
    if (!m_videoFrameMailbox.markDeliveryScheduled())
        return;

    const uint64_t generation = m_videoFrameGeneration.load(std::memory_order_acquire);
    QPointer<PlaybackRuntimeBridge> self(this);
    QMetaObject::invokeMethod(this, [self, generation]() {
        if (auto bridge = self.data())
            bridge->drainLatestVideoFrame(generation);
    }, Qt::QueuedConnection);
}

void PlaybackRuntimeBridge::drainLatestVideoFrame(uint64_t generation)
{
    if (generation != m_videoFrameGeneration.load(std::memory_order_acquire))
        return;

    std::shared_ptr<VideoFrame> frame = m_videoFrameMailbox.takeLatest();
    m_videoFrameMailbox.deliveryCompleted();
    if (frame) {
        m_presentedVideoFrames.fetch_add(1, std::memory_order_relaxed);
        emit SigVideoFrame(std::move(frame));
    }

    if (m_videoFrameMailbox.hasPending())
        scheduleVideoFrameDelivery();
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
        auto* bridge = self.data();
        bridge->m_publishedVideoFrames.fetch_add(1, std::memory_order_relaxed);
        bridge->m_videoFrameMailbox.publish(std::move(frame));
        bridge->scheduleVideoFrameDelivery();
    }));

    m_connections.emplace_back(runtime->SigSubtitleFrame.connect([self](std::shared_ptr<const SubtitleFrame> frame) {
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, frame = std::move(frame)]() {
            if (auto bridge = self.data())
                emit bridge->SigSubtitleFrame(frame);
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

    m_connections.emplace_back(runtime->SigPlaybackStatus.connect([self](const PlaybackStatus& status) {
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, status]() {
            if (auto bridge = self.data())
                emit bridge->SigPlaybackStatus(status);
        }, Qt::QueuedConnection);
    }));

    m_connections.emplace_back(runtime->SigMediaInfo.connect([self](const MediaInfo& info) {
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, info]() {
            if (auto bridge = self.data())
                emit bridge->SigMediaInfo(info);
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
