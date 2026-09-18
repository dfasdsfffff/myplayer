#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "media_source.h"
#include "latest_video_frame_mailbox.h"
#include "signal.h"
#include "subtitle_frame.h"
#include "video_frame.h"

class PlaybackRuntime;

class PlaybackRuntimeBridge : public QObject
{
	Q_OBJECT

public:
	explicit PlaybackRuntimeBridge(QObject* parent = nullptr);
	~PlaybackRuntimeBridge() override;

	void attach(PlaybackRuntime* runtime);

	PlaybackRuntimeBridge(const PlaybackRuntimeBridge&) = delete;
	PlaybackRuntimeBridge& operator=(const PlaybackRuntimeBridge&) = delete;

signals:
	void SigPlayMsg(const QString& strMsg);
	void SigFrameDimensionsChanged(int nFrameWidth, int nFrameHeight);
	void SigVideoFrame(std::shared_ptr<VideoFrame> frame);
	void SigSubtitleFrame(std::shared_ptr<const SubtitleFrame> frame);
	void SigVideoTotalSeconds(int nSeconds);
	void SigVideoPlaySeconds(int nSeconds);
	void SigVideoVolume(double dPercent);
	void SigPauseStat(bool bPaused);
	void SigStop();
	void SigStopFinished();
	void SigStartPlay(const QString& strFileName);
	void SigPlaybackStatus(PlaybackStatus status);
	void SigMediaInfo(MediaInfo info);
	void SigPlayNextOne();
	void SigRandomPlayOne();

private:
	void detach();
    void scheduleVideoFrameDelivery();
    void drainLatestVideoFrame(uint64_t generation);

	PlaybackRuntime* m_runtime = nullptr;
	std::vector<sigslot::scoped_connection> m_connections;
    LatestVideoFrameMailbox m_videoFrameMailbox;
    std::atomic<uint64_t> m_videoFrameGeneration{0};
    std::atomic<uint64_t> m_publishedVideoFrames{0};
    std::atomic<uint64_t> m_presentedVideoFrames{0};
};
