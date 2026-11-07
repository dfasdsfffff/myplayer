#include "xcamera_widget.h"

#include "xcamera_config.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>

namespace {

bool IsCameraList(const QListWidget *list) {
  return list && list->objectName() == QStringLiteral("cam_list");
}

} // namespace

XCameraWidget::XCameraWidget(QWidget *parent) : CoreVideoWidget(parent) {
  setAcceptDrops(true);
}

void XCameraWidget::stop() {
  CoreVideoWidget::stop();
  emit previewClosed();
}

void XCameraWidget::dragEnterEvent(QDragEnterEvent *event) {
  auto *cameraList = event
                            ? qobject_cast<QListWidget *>(event->source())
                            : nullptr;
  if (!IsCameraList(cameraList)) {
    if (event)
      event->ignore();
    return;
  }

  event->acceptProposedAction();
}

void XCameraWidget::dropEvent(QDropEvent *event) {
  auto *cameraList = event
                           ? qobject_cast<QListWidget *>(event->source())
                           : nullptr;
  if (!IsCameraList(cameraList)) {
    if (event)
      event->ignore();
    return;
  }

  auto *item = cameraList->currentItem();
  if (!item) {
    event->ignore();
    return;
  }

  bool roleIsValid = false;
  const int roleIndex = item->data(Qt::UserRole).toInt(&roleIsValid);
  const int index = roleIsValid ? roleIndex : cameraList->row(item);
  if (index < 0) {
    event->ignore();
    return;
  }

  const XCameraData camera = XCameraConfig::Instance()->GetCam(index);
  if (camera.sub_url[0] == '\0' || !Open(camera.sub_url)) {
    event->ignore();
    return;
  }

  emit cameraAssigned(QString::fromLocal8Bit(camera.name));
  event->acceptProposedAction();
}

bool XCameraWidget::Open(const char *url) {
  if (!url || *url == '\0')
    return false;

  return open(QString::fromLocal8Bit(url), true);
}

void XCameraWidget::Draw() {
  // Playback and SDL presentation are owned by CoreVideoWidget now.
}

void XCameraWidget::paintEvent(QPaintEvent *event) {
  if (hasFrame()) { CoreVideoWidget::paintEvent(event); return; }
  QPainter painter(this);
  painter.fillRect(rect(), QColor("#0c1219"));
  painter.setPen(QColor("#718497"));
  auto font = painter.font();
  font.setPixelSize(12);
  painter.setFont(font);
  painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("拖入设备开始预览"));
}
