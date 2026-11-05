#define SDL_MAIN_HANDLED
#include "core_video_widget.h"
#include "network_input.h"
#include <SDL.h>
#include <QMetaObject>
#include <QPaintEvent>
#include <algorithm>
#include <cmath>
#include <utility>

CoreVideoWidget::CoreVideoWidget(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(32, 24);
    timer_.setInterval(33);
    connect(&timer_, &QTimer::timeout, this, &CoreVideoWidget::present);
}

CoreVideoWidget::~CoreVideoWidget() {
    stop();
    destroyRenderer();
}

bool CoreVideoWidget::open(const QString& location, bool muted) {
    stop();
    if (location.trimmed().isEmpty()) return false;
    runtime_ = PlaybackRuntime::Create();
    if (!runtime_) {
        emit playbackError(tr("无法初始化播放核心"));
        return false;
    }
    const auto generation = generation_;
    connections_.emplace_back(runtime_->SigVideoFrame.connect([this](std::shared_ptr<VideoFrame> frame) {
        std::lock_guard<std::mutex> lock(frameMutex_);
        latest_ = std::move(frame);
    }));
    connections_.emplace_back(runtime_->SigVideoTotalSeconds.connect([this, generation](int seconds) {
        QMetaObject::invokeMethod(this, [this, generation, seconds] {
            if (generation != generation_) return;
            duration_ = seconds;
            emit durationChanged(duration_);
        }, Qt::QueuedConnection);
    }));
    connections_.emplace_back(runtime_->SigVideoPlaySeconds.connect([this, generation](int seconds) {
        QMetaObject::invokeMethod(this, [this, generation, seconds] {
            if (generation != generation_) return;
            position_ = seconds;
            emit positionChanged(position_);
        }, Qt::QueuedConnection);
    }));
    connections_.emplace_back(runtime_->SigPauseStat.connect([this, generation](bool paused) {
        QMetaObject::invokeMethod(this, [this, generation, paused] {
            if (generation != generation_) return;
            paused_ = paused;
            emit pausedChanged(paused);
        }, Qt::QueuedConnection);
    }));
    connections_.emplace_back(runtime_->SigPlaybackStatus.connect([this, generation](const PlaybackStatus& status) {
        if (status.state != PlaybackState::Failed) return;
        const auto error = status.error;
        QMetaObject::invokeMethod(this, [this, generation, error] {
            if (generation == generation_)
                emit playbackError(tr("播放失败（错误 %1）").arg(static_cast<int>(error)));
        }, Qt::QueuedConnection);
    }));
    connections_.emplace_back(runtime_->SigStop.connect([this, generation] {
        QMetaObject::invokeMethod(this, [this, generation] {
            if (generation == generation_) stop();
        }, Qt::QueuedConnection);
    }));
    runtime_->controller().setVolume(muted ? 0.0 : 1.0);
    runtime_->controller().setLoopPolicy(VideoLoopPolicy::LOOP_NONE);
    MediaSource source{location.toUtf8().toStdString()};
    source.enableAudio = !muted;
    if (!runtime_->controller().play(source)) {
        stop();
        emit playbackError(tr("无法打开媒体"));
        return false;
    }
    timer_.start();
    return true;
}

void CoreVideoWidget::stop() {
    ++generation_;
    timer_.stop();
    if (runtime_) runtime_->controller().stopAndWait();
    connections_.clear();
    runtime_.reset();
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        latest_.reset();
    }
    current_.reset();
    presentedFrames_ = 0;
    paused_ = false;
    duration_ = position_ = 0;
    if (renderer_) {
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        SDL_RenderPresent(renderer_);
    }
}

void CoreVideoWidget::togglePause() { if (runtime_) runtime_->controller().pause(); }
void CoreVideoWidget::seek(double fraction) {
    if (runtime_ && std::isfinite(fraction)) runtime_->controller().seek(std::clamp(fraction, 0.0, 1.0));
}
void CoreVideoWidget::setSpeed(double speed) {
    if (runtime_ && std::isfinite(speed)) runtime_->controller().setSpeed(std::clamp(speed, 0.2, 2.0));
}

bool CoreVideoWidget::ensureRenderer() {
    if (renderer_) return true;
    if (!videoInitialized_) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) return false;
        videoInitialized_ = true;
    }
    window_ = SDL_CreateWindowFrom(reinterpret_cast<void*>(winId()));
    if (!window_) { destroyRenderer(); return false; }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer_) { destroyRenderer(); return false; }
    return true;
}

void CoreVideoWidget::destroyRenderer() {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    texture_ = nullptr; renderer_ = nullptr; window_ = nullptr;
    textureSize_ = {}; textureFormat_ = 0;
    if (videoInitialized_) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    videoInitialized_ = false;
}

void CoreVideoWidget::present() {
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        if (latest_) current_ = std::exchange(latest_, {});
    }
    if (!current_ || !isVisible() || width() <= 0 || height() <= 0) return;
    if (!ensureRenderer()) return;
    const auto& frame = *current_;
    const QSize size(frame.width, frame.height);
    const Uint32 format = frame.format == VideoFrameFormat::Yuv420P ? SDL_PIXELFORMAT_IYUV : SDL_PIXELFORMAT_BGRA32;
    if (!texture_ || textureSize_ != size || textureFormat_ != format) {
        if (texture_) SDL_DestroyTexture(texture_);
        texture_ = SDL_CreateTexture(renderer_, format, SDL_TEXTUREACCESS_STREAMING, size.width(), size.height());
        textureSize_ = size; textureFormat_ = format;
        if (!texture_) return;
    }
    SDL_SetYUVConversionMode(frame.colorRange == AVCOL_RANGE_JPEG ? SDL_YUV_CONVERSION_JPEG :
        frame.colorSpace == AVCOL_SPC_BT709 ? SDL_YUV_CONVERSION_BT709 : SDL_YUV_CONVERSION_BT601);
    const int result = frame.format == VideoFrameFormat::Yuv420P
        ? SDL_UpdateYUVTexture(texture_, nullptr, frame.planes[0].data.get(), frame.planes[0].stride,
            frame.planes[1].data.get(), frame.planes[1].stride, frame.planes[2].data.get(), frame.planes[2].stride)
        : SDL_UpdateTexture(texture_, nullptr, frame.bgra.data(), frame.bytesPerLine);
    if (result != 0) return;
    int viewportWidth = width(), viewportHeight = height();
    SDL_GetRendererOutputSize(renderer_, &viewportWidth, &viewportHeight);
    const auto rect = ComputeVideoDisplayRect(frame.width, frame.height, frame.sampleAspectRatio,
        frame.rotationDegrees, viewportWidth, viewportHeight);
    SDL_Rect destination{rect.x, rect.y, rect.width, rect.height};
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    if (SDL_RenderClear(renderer_) != 0 ||
        SDL_RenderCopyEx(renderer_, texture_, nullptr, &destination, frame.rotationDegrees, nullptr, SDL_FLIP_NONE) != 0)
        return;
    SDL_RenderPresent(renderer_);
    ++presentedFrames_;
}

void CoreVideoWidget::paintEvent(QPaintEvent*) { present(); }
void CoreVideoWidget::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); present(); }
void CoreVideoWidget::showEvent(QShowEvent* event) { QWidget::showEvent(event); present(); }
