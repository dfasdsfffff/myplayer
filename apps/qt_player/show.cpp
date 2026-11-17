#include "show.h"
#include "ui_show.h"
#include "globalhelper.h"
#include "app_playback.h"
#include <QFileInfo>
#include <QMimeData>
#include <QLabel>
Show::Show(QWidget* parent) : QWidget(parent), ui(new Ui::Show), view_(nullptr) {
    ui->setupUi(this);
    ui->label->setAutoFillBackground(true);
    auto palette = ui->label->palette(); palette.setColor(QPalette::Window, Qt::black); ui->label->setPalette(palette);
    ui->label->hide();
    view_ = new player_sdk_qt::VideoWidget(this);
    setStyleSheet(GlobalHelper::GetThemeStr("://res/qss/show.css"));
    setAttribute(Qt::WA_StyledBackground);
    m_emptyState = new QLabel(QStringLiteral("<div style='font-size:24px;color:#EDF2F7'>MyPlayer</div><br>拖放视频到这里，或按 Ctrl+O 打开文件"), this);
    m_emptyState->setObjectName(QStringLiteral("EmptyState"));
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_playbackBadge = new QLabel(this);
    m_playbackBadge->setObjectName(QStringLiteral("PlaybackBadge"));
    m_videoInfoBadge = new QLabel(this);
    m_videoInfoBadge->setObjectName(QStringLiteral("VideoInfoBadge"));
    for (auto* label : {m_emptyState, m_playbackBadge, m_videoInfoBadge}) {
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
#ifdef Q_OS_WIN
        // Alien overlays make Qt repaint over the native SDL child surface.
        // Native siblings let the window system clip the video correctly.
        label->setAttribute(Qt::WA_NativeWindow);
#endif
    }
    m_playbackBadge->hide();
    m_videoInfoBadge->hide();
    LayoutOverlays();
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
    if (frame) { m_emptyState->hide(); ClearAudioOnlyIndicator(); }
    view_->setVideoFrame(std::move(frame));
}
void Show::OnSubtitleFrame(std::shared_ptr<const SubtitleFrame> frame) { view_->setSubtitleFrame(std::move(frame)); }
void Show::OnVideoPlaySeconds(int seconds) { if (!m_playbackController) view_->setPosition(static_cast<qint64>(seconds)*1000); }
bool Show::LoadExternalSubtitleFile(const QString& file) { return view_->loadExternalSubtitle(file); }
bool Show::UnloadExternalSubtitleFile() { const bool had = view_->hasExternalSubtitle(); view_->clearExternalSubtitle(); return had; }
bool Show::HasExternalSubtitle() const { return view_->hasExternalSubtitle(); }
void Show::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); if (view_) view_->setGeometry(rect()); ui->label->setGeometry(rect()); LayoutOverlays(); }
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
void Show::OnStopFinished() { view_->setVideoFrame({}); view_->setSubtitleFrame({}); view_->clearExternalSubtitle(); ClearAudioOnlyIndicator(); m_playbackBadge->hide(); m_videoInfoBadge->hide(); m_emptyState->show(); m_emptyState->raise(); }
void Show::ShowAudioOnly(const QString& location) {
    m_emptyState->hide();
    view_->setVideoFrame({}); view_->hide(); m_audioOnlyActive = true;
    ui->label->setGeometry(rect()); ui->label->setText(QString("%1\n正在播放音频").arg(QFileInfo(location).fileName()));
    ui->label->show();
}
void Show::ClearAudioOnlyIndicator() { m_audioOnlyActive = false; ui->label->clear(); ui->label->hide(); view_->show(); }

void Show::LayoutOverlays()
{
    if (!m_emptyState || !m_playbackBadge || !m_videoInfoBadge) return;
    m_emptyState->setGeometry(rect());
    m_playbackBadge->adjustSize();
    m_videoInfoBadge->adjustSize();
    m_playbackBadge->move(20, 20);
    m_videoInfoBadge->move(qMax(20, width() - m_videoInfoBadge->width() - 20), 20);
    m_emptyState->raise();
    m_playbackBadge->raise();
    m_videoInfoBadge->raise();
}

void Show::SetPlaybackBadge(const QString& text)
{
    m_emptyState->hide();
    m_playbackBadge->setText(QStringLiteral("●  ") + text);
    m_playbackBadge->setVisible(!text.isEmpty());
    LayoutOverlays();
}

void Show::SetMediaDetails(const QString& text)
{
    m_videoInfoBadge->setText(text);
    m_videoInfoBadge->setVisible(!text.isEmpty());
    LayoutOverlays();
}
