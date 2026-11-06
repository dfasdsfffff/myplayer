#pragma once
#include <QObject>
#include <QPointer>
#include "media_source.h"
#include "video_frame.h"
class AppPlaybackSession;
class PlaybackRuntimeBridge : public QObject {
    Q_OBJECT
public:
    explicit PlaybackRuntimeBridge(QObject* parent = nullptr);
    void attach(AppPlaybackSession* session);
signals:
    void SigPlayMsg(const QString& message);
    void SigFrameDimensionsChanged(int width,int height);
    void SigVideoFrame(std::shared_ptr<const VideoFrame> frame);
    void SigSubtitleFrame(std::shared_ptr<const SubtitleFrame> frame);
    void SigVideoTotalSeconds(int seconds);
    void SigVideoPlaySeconds(int seconds);
    void SigVideoVolume(double volume);
    void SigPauseStat(bool paused);
    void SigStop();
    void SigStopFinished();
    void SigStartPlay(const QString& location);
    void SigPlaybackStatus(PlaybackStatus status);
    void SigMediaInfo(MediaInfo info);
    void SigPlayNextOne();
    void SigRandomPlayOne();
private:
    QPointer<QObject> player_;
};
