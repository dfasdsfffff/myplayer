#pragma once
#include <player_sdk_qt/video_widget.h>
#include <player_sdk_qt/qt_player.h>
class CoreVideoWidget : public player_sdk_qt::VideoWidget {
    Q_OBJECT
public:
    explicit CoreVideoWidget(QWidget* parent = nullptr);
    ~CoreVideoWidget() override;
    bool open(const QString& location, bool muted = true);
    void stop();
    void togglePause();
    void seek(double fraction);
    void setSpeed(double speed);
    bool isPaused() const { return paused_; }
    qint64 durationSeconds() const { return duration_; }
    qint64 positionSeconds() const { return position_; }
    bool hasFrame() const { return hasFrame_; }
    quint64 presentedFrames() const { return VideoWidget::presentedFrames(); }
signals:
    void durationChanged(qint64 seconds);
    void positionChanged(qint64 seconds);
    void pausedChanged(bool paused);
    void playbackError(const QString& message);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    player_sdk_qt::QtPlayer* player_{nullptr};
    bool paused_{false}, hasFrame_{false};
    qint64 duration_{0}, position_{0};
};
