#include "show.h"
#include "ui_show.h"
#include "globalhelper.h"
#include "app_playback.h"
#include <QFileInfo>
#include <QMimeData>
Show::Show(QWidget* parent) : QWidget(parent), ui(new Ui::Show), view_(new player_sdk_qt::VideoWidget(this)) {
    ui->setupUi(this);
    ui->label->setAutoFillBackground(true);
    auto palette = ui->label->palette(); palette.setColor(QPalette::Window, Qt::black); ui->label->setPalette(palette);
    ui->label->hide();
    setStyleSheet(GlobalHelper::GetThemeStr("://res/qss/show.css"));
    setAcceptDrops(true); setMouseTracking(true);
    view_->setAttribute(Qt::WA_TransparentForMouseEvents);
    view_->setGeometry(rect());
    connect(view_,&player_sdk_qt::VideoWidget::framePresented,this,[this] { if (m_measureFrameSubmission) emit SigVideoFrameSubmitted(); });
}
Show::~Show() { delete ui; }
bool Show::Init() { connect(this,&Show::SigPlay,this,&Show::OnPlay,Qt::UniqueConnection); return true; }
void Show::SetPlaybackController(AppPlaybackController* controller) {
    m_playbackController = controller;
    if (controller) connect(&controller->player,&player_sdk_qt::QtPlayer::positionChanged,view_,&player_sdk_qt::VideoWidget::setPosition);
}
void Show::SetFrameSubmissionMeasurementEnabled(bool enabled) { m_measureFrameSubmission = enabled; }
void Show::OnFrameDimensionsChanged(int,int) {}
void Show::OnVideoFrame(std::shared_ptr<const VideoFrame> frame) {
    if (frame) ClearAudioOnlyIndicator(); view_->setVideoFrame(std::move(frame));
}
void Show::OnSubtitleFrame(std::shared_ptr<const SubtitleFrame> frame) { view_->setSubtitleFrame(std::move(frame)); }
void Show::OnVideoPlaySeconds(int seconds) { if (!m_playbackController) view_->setPosition(static_cast<qint64>(seconds)*1000); }
bool Show::LoadExternalSubtitleFile(const QString& file) { return view_->loadExternalSubtitle(file); }
bool Show::UnloadExternalSubtitleFile() { const bool had = view_->hasExternalSubtitle(); view_->clearExternalSubtitle(); return had; }
bool Show::HasExternalSubtitle() const { return view_->hasExternalSubtitle(); }
void Show::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); view_->setGeometry(rect()); ui->label->setGeometry(rect()); }
void Show::dragEnterEvent(QDragEnterEvent* event) { event->acceptProposedAction(); }
void Show::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (!urls.isEmpty()) emit SigOpenFile(urls.first().isLocalFile() ? urls.first().toLocalFile() : urls.first().toString());
}
void Show::keyReleaseEvent(QKeyEvent* event) {
    switch(event->key()) {
    case Qt::Key_Left: emit SigSeekBack(); break;
    case Qt::Key_Right: emit SigSeekForward(); break;
    case Qt::Key_Up: emit SigAddVolume(); break;
    case Qt::Key_Down: emit SigSubVolume(); break;
    default: QWidget::keyReleaseEvent(event); break;
    }
}
void Show::mousePressEvent(QMouseEvent* event) { if (event->buttons() & Qt::RightButton) emit SigShowMenu(); QWidget::mousePressEvent(event); }
void Show::OnPlay(QString file) { if (!file.isEmpty()) view_->clearExternalSubtitle(); if (m_playbackController && !file.isEmpty()) m_playbackController->play(file.toUtf8().toStdString()); }
void Show::OnStopFinished() { view_->setVideoFrame({}); view_->setSubtitleFrame({}); view_->clearExternalSubtitle(); ClearAudioOnlyIndicator(); }
void Show::ShowAudioOnly(const QString& location) {
    view_->setVideoFrame({}); view_->hide(); m_audioOnlyActive = true;
    ui->label->setGeometry(rect()); ui->label->setText(QString("%1\n正在播放音频").arg(QFileInfo(location).fileName()));
    ui->label->show();
}
void Show::ClearAudioOnlyIndicator() { m_audioOnlyActive = false; ui->label->clear(); ui->label->hide(); view_->show(); }
