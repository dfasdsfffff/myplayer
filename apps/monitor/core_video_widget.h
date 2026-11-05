#pragma once

#include <QWidget>
#include <QTimer>
#include "playback_runtime.h"
#include <mutex>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

class CoreVideoWidget : public QWidget {
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
    bool hasFrame() const { return static_cast<bool>(current_); }
    quint64 presentedFrames() const { return presentedFrames_; }

signals:
    void durationChanged(qint64 seconds);
    void positionChanged(qint64 seconds);
    void pausedChanged(bool paused);
    void playbackError(const QString& message);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void showEvent(QShowEvent*) override;

private:
    void present();
    bool ensureRenderer();
    void destroyRenderer();
    std::unique_ptr<PlaybackRuntime> runtime_;
    std::vector<sigslot::scoped_connection> connections_;
    QTimer timer_;
    std::mutex frameMutex_;
    std::shared_ptr<VideoFrame> latest_;
    std::shared_ptr<VideoFrame> current_;
    SDL_Window* window_{nullptr};
    SDL_Renderer* renderer_{nullptr};
    SDL_Texture* texture_{nullptr};
    QSize textureSize_;
    unsigned int textureFormat_{0};
    bool videoInitialized_{false};
    bool paused_{false};
    qint64 duration_{0};
    qint64 position_{0};
    quint64 generation_{0};
    quint64 presentedFrames_{0};
};
