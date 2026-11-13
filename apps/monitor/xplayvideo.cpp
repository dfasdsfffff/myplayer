#include "xplayvideo.h"

#include "ui_xplayvideo.h"
#include <QCloseEvent>
#include <QIcon>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QtGlobal>
#include <limits>

XPlayVideo::XPlayVideo(QWidget *parent) : QDialog(parent), ui(new Ui::XPlayVideo) {
  ui->setupUi(this);
  ui->speed->setRange(2, 20);
  ui->speed->setValue(10);
  ui->speedtext->setText(QStringLiteral("1.0x"));

  auto *videoLayout = new QVBoxLayout(ui->video);
  videoLayout->setContentsMargins(0, 0, 0, 0);
  view_ = new CoreVideoWidget(ui->video);
  videoLayout->addWidget(view_);

  connect(ui->speed, &QSlider::sliderReleased, this, &XPlayVideo::SetSpeed);
  connect(ui->pos, &QSlider::sliderPressed, this, [this] { seeking_ = true; });
  connect(ui->pos, &QSlider::sliderMoved, this, &XPlayVideo::Move);
  connect(ui->pos, &QSlider::sliderReleased, this, &XPlayVideo::PlayPos);
  connect(ui->pause, &QPushButton::clicked, this, &XPlayVideo::Pause);

  connect(view_, &CoreVideoWidget::durationChanged, this,
          [this](qint64 durationSeconds) {
            if (!ui)
              return;
            const qint64 boundedDuration = qBound<qint64>(
                0, durationSeconds, static_cast<qint64>(std::numeric_limits<int>::max()));
            ui->pos->setRange(0, static_cast<int>(boundedDuration));
          });
  connect(view_, &CoreVideoWidget::positionChanged, this,
          &XPlayVideo::updatePosition);
  connect(view_, &CoreVideoWidget::pausedChanged, this,
          &XPlayVideo::updatePauseButton);
}

XPlayVideo::~XPlayVideo() {
  Close();
  delete view_;
  view_ = nullptr;
  delete ui;
  ui = nullptr;
}

bool XPlayVideo::Open(const char *url) {
  if (!url || *url == '\0' || !view_)
    return false;

  if (!view_->open(QString::fromLocal8Bit(url), false))
    return false;

  seeking_ = false;
  SetSpeed();
  updatePauseButton(view_->isPaused());
  return true;
}

void XPlayVideo::SetSpeed() {
  if (!ui || !view_)
    return;

  const double speed = qBound(0.2, ui->speed->value() / 10.0, 2.0);
  view_->setSpeed(speed);
  ui->speedtext->setText(QString::number(speed, 'f', 1) + QStringLiteral("x"));
}

void XPlayVideo::Move() {
  // Do not pause here: seeking must preserve the state reported by the core.
  seeking_ = true;
}

void XPlayVideo::PlayPos() {
  if (!ui || !view_)
    return;

  const qint64 durationSeconds = view_->durationSeconds();
  const qint64 positionSeconds = ui->pos->value();
  if (durationSeconds > 0) {
    const double fraction = qBound(
        0.0, static_cast<double>(positionSeconds) / durationSeconds, 1.0);
    view_->seek(fraction);
  }
  seeking_ = false;
}

void XPlayVideo::Pause() {
  if (view_)
    view_->togglePause();
}

void XPlayVideo::Close() {
  if (view_)
    view_->stop();
  seeking_ = false;
}

void XPlayVideo::updatePosition(qint64 positionSeconds) {
  if (!ui || seeking_)
    return;

  const qint64 boundedPosition = qBound<qint64>(
      0, positionSeconds, static_cast<qint64>(std::numeric_limits<int>::max()));
  ui->pos->setValue(static_cast<int>(boundedPosition));
}

void XPlayVideo::updatePauseButton(bool paused) {
  if (!ui)
    return;

  ui->pause->setIcon(QIcon(paused ? QStringLiteral(":/img/play.png")
                              : QStringLiteral(":/img/pause.png")));
}

void XPlayVideo::closeEvent(QCloseEvent *event) {
  Close();
  QDialog::closeEvent(event);
}
