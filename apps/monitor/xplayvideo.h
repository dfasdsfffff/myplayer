#ifndef XPLAYVIDEO_H
#define XPLAYVIDEO_H

#include "core_video_widget.h"
#include <QDialog>

namespace Ui {
class XPlayVideo;
}

class QCloseEvent;

class XPlayVideo : public QDialog {
  Q_OBJECT

public:
  explicit XPlayVideo(QWidget *parent = nullptr);
  ~XPlayVideo() override;

  bool Open(const char *url);

public slots:
  // Sets playback speed from the 2..20 slider as 0.2x..2.0x.
  void SetSpeed();
  // Marks the position slider as being manipulated by the user.
  void Move();
  // Seeks to the position selected by the user.
  void PlayPos();
  // Toggles playback using CoreVideoWidget's state.
  void Pause();

protected:
  void Close();
  void closeEvent(QCloseEvent *event) override;

private:
  void updatePosition(qint64 positionSeconds);
  void updatePauseButton(bool paused);

  CoreVideoWidget *view_ = nullptr;
  Ui::XPlayVideo *ui = nullptr;
  bool seeking_ = false;
};

#endif // XPLAYVIDEO_H
