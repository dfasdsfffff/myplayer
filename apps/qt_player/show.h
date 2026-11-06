#pragma once
#include <QWidget>
#include <QTimer>
#include <QDragEnterEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QActionGroup>
#include "video_frame.h"
#include <player_sdk_qt/video_widget.h>
class AppPlaybackController;
namespace Ui { class Show; }
class Show : public QWidget {
    Q_OBJECT
public:
    explicit Show(QWidget* parent);
    ~Show();
    bool Init();
    void SetPlaybackController(AppPlaybackController* controller);
    void SetFrameSubmissionMeasurementEnabled(bool enabled);
    void OnPlay(QString file);
    void OnStopFinished();
    void ShowAudioOnly(const QString& location);
    void ClearAudioOnlyIndicator();
    void OnFrameDimensionsChanged(int width,int height);
    void OnVideoFrame(std::shared_ptr<const VideoFrame> frame);
    void OnSubtitleFrame(std::shared_ptr<const SubtitleFrame> frame);
    void OnVideoPlaySeconds(int seconds);
    bool LoadExternalSubtitleFile(const QString& filename);
    bool UnloadExternalSubtitleFile();
    bool HasExternalSubtitle() const;
signals:
    void SigOpenFile(QString filename);
    void SigPlay(QString file);
    void SigFullScreen();
    void SigPlayOrPause();
    void SigStop();
    void SigShowMenu();
    void SigSeekForward();
    void SigSeekBack();
    void SigAddVolume();
    void SigSubVolume();
    // QPainter submission, not compositor/display visibility.
    void SigVideoFrameSubmitted();
protected:
    void dropEvent(QDropEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
private:
    Ui::Show* ui;
    player_sdk_qt::VideoWidget* view_;
    AppPlaybackController* m_playbackController{nullptr};
    bool m_measureFrameSubmission{false};
    bool m_audioOnlyActive{false};
};
