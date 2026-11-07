#include "core_video_widget.h"
#include <QPainter>
#include <algorithm>
#include <cmath>
CoreVideoWidget::CoreVideoWidget(QWidget* parent) : VideoWidget(parent) {}
CoreVideoWidget::~CoreVideoWidget() { setPlayer(nullptr); delete player_; }
bool CoreVideoWidget::open(const QString& location, bool muted) {
    if (location.trimmed().isEmpty()) return false;
    if (!player_) {
        try {
            player_ = new player_sdk_qt::QtPlayer(this);
        } catch (const std::exception& error) {
            emit playbackError(QString::fromUtf8(error.what()));
            return false;
        }
    setPlayer(player_);
    connect(player_, &player_sdk_qt::QtPlayer::mediaInfoChanged, this, [this](auto info) {
        duration_ = info && info->duration ? info->duration->count()/1000 : 0; emit durationChanged(duration_);
    });
    connect(player_, &player_sdk_qt::QtPlayer::positionChanged, this, [this](qint64 ms) { position_ = ms/1000; emit positionChanged(position_); });
    connect(player_, &player_sdk_qt::QtPlayer::stateChanged, this, [this](auto state) {
        paused_ = state == play_core::PlayerState::Paused; emit pausedChanged(paused_);
    });
    connect(player_, &player_sdk_qt::QtPlayer::videoFrameChanged, this, [this](auto frame) { hasFrame_ = bool(frame); });
    connect(player_, &player_sdk_qt::QtPlayer::errorOccurred, this, &CoreVideoWidget::playbackError);
    }
    play_core::MediaSource source{location.toUtf8().toStdString()}; source.enableAudio = !muted;
    const auto request = player_->open(std::move(source));
    if (!request) return false;
    player_->setVolume(muted ? 0 : 1); return bool(player_->play());
}
void CoreVideoWidget::paintEvent(QPaintEvent* event) {
    if (hasFrame_) { VideoWidget::paintEvent(event); return; }
    QPainter painter(this); painter.fillRect(rect(),QColor("#263039"));
    painter.fillRect(QRect(0,0,width(),20),QColor("#111820"));
    painter.setPen(QColor("#a5b0bb")); auto font = painter.font(); font.setPixelSize(11); painter.setFont(font);
    painter.drawText(QRect(8,0,width()-16,20),Qt::AlignVCenter,tr("预览窗口"));
    painter.setPen(QColor("#8996a3")); painter.drawText(rect(),Qt::AlignCenter,tr("拖入摄像机开始预览"));
}
void CoreVideoWidget::stop() { if (player_) player_->close(); setVideoFrame({}); hasFrame_ = false; paused_ = false; duration_ = position_ = 0; }
void CoreVideoWidget::togglePause() { if (player_) player_->setPaused(!paused_); }
void CoreVideoWidget::seek(double fraction) {
    if (player_ && std::isfinite(fraction)) {
        auto info = player_->mediaInfo();
        if (info && info->duration) player_->seek(static_cast<qint64>(std::clamp(fraction,0.,1.) * info->duration->count()));
    }
}
void CoreVideoWidget::setSpeed(double speed) { if (player_ && std::isfinite(speed)) player_->setPlaybackRate(std::clamp(speed,.1,2.)); }
