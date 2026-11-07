#ifndef XCAMERA_WIDGET_H
#define XCAMERA_WIDGET_H

#include "core_video_widget.h"

class QDragEnterEvent;
class QDropEvent;

class XCameraWidget : public CoreVideoWidget {
  Q_OBJECT

public:
  explicit XCameraWidget(QWidget *parent = nullptr);
  ~XCameraWidget() override = default;

  // Kept for compatibility with the old viewer timer loop. Playback is now
  // driven by CoreVideoWidget itself.
  void Draw();

public slots:
  void stop() override;

signals:
  void cameraAssigned(const QString& name);
  void previewClosed();

protected:
  void paintEvent(QPaintEvent *event) override;
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dropEvent(QDropEvent *event) override;

  // Opens the configured camera URL using the local 8-bit encoding used by
  // XCameraConfig.
  bool Open(const char *url);
};

#endif // XCAMERA_WIDGET_H
