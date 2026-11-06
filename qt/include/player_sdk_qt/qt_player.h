#pragma once
#include <QObject>
#include <QUrl>
#include <play_core/player.h>
#include <memory>
namespace player_sdk_qt {
class QtPlayer final : public QObject {
    Q_OBJECT
public:
    explicit QtPlayer(QObject* parent = nullptr);
    ~QtPlayer() override;
    play_core::Request open(const QUrl& url, bool audio = true);
    play_core::Request open(play_core::MediaSource source);
    play_core::Request play();
    play_core::Request setPaused(bool paused);
    play_core::Request seek(qint64 milliseconds);
    play_core::Request stop();
    play_core::Request close();
    play_core::Request setHardwareDecode(play_core::HardwareDecode preference);
    play_core::Request setVolume(double volume);
    play_core::Request setPlaybackRate(double rate);
    play_core::Request selectAudioTrack(int id);
    play_core::Request selectSubtitleTrack(std::optional<int> id);
    play_core::PlayerState state() const;
    std::shared_ptr<const play_core::MediaInfo> mediaInfo() const;
signals:
    void stateChanged(play_core::PlayerState state);
    void mediaInfoChanged(std::shared_ptr<const play_core::MediaInfo> info);
    void positionChanged(qint64 milliseconds);
    void volumeChanged(double volume);
    void videoFrameChanged(std::shared_ptr<const play_core::VideoFrame> frame);
    void subtitleChanged(std::shared_ptr<const play_core::SubtitleFrame> frame);
    void operationFinished(play_core::OperationResult result);
    void errorOccurred(const QString& message);
    void errorDetails(play_core::PlayerError error);
    void ended();
    void closed();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void drain();
    play_core::Request track(play_core::Request request);
};
}
Q_DECLARE_METATYPE(play_core::PlayerError)
Q_DECLARE_METATYPE(play_core::PlayerState)
Q_DECLARE_METATYPE(play_core::OperationResult)
Q_DECLARE_METATYPE(std::shared_ptr<const play_core::MediaInfo>)
Q_DECLARE_METATYPE(std::shared_ptr<const play_core::VideoFrame>)
Q_DECLARE_METATYPE(std::shared_ptr<const play_core::SubtitleFrame>)
