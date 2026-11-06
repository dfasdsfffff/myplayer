#pragma once
#include <QWidget>
#include <QImage>
#include <QPointer>
#include <play_core/video_frame.h>
#include <memory>
namespace player_sdk_qt {
class QtPlayer;
// One view per player. Binding either object's destruction automatically clears
// the view. No window handles are borrowed by Core.
class VideoWidget : public QWidget {
    Q_OBJECT
public:
    explicit VideoWidget(QWidget* parent = nullptr);
    ~VideoWidget() override;
    void setPlayer(QtPlayer* player);
    QtPlayer* player() const;
    bool loadExternalSubtitle(const QString& filename);
    void clearExternalSubtitle();
    quint64 presentedFrames() const;
    // Manual CPU frame presentation is also available without binding a player.
    void setVideoFrame(std::shared_ptr<const play_core::VideoFrame> frame);
    void setSubtitleFrame(std::shared_ptr<const play_core::SubtitleFrame> frame);
    void setPosition(qint64 milliseconds);
    bool hasExternalSubtitle() const;
    // Explicit SDL renderer readback including subtitles; not used in playback.
    QImage grabVideoFrame();
signals:
    // Emitted for a fresh frame after SDL_RenderPresent returns; not a monitor/compositor fence.
    void framePresented();
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
private:
    void present();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
