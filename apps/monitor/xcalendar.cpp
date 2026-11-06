#include "xcalendar.h"
#include <Qpainter>

XCalendar::XCalendar(QWidget *parent) : QCalendarWidget(parent) {}

void XCalendar::paintCell(QPainter *painter, const QRect &rect,
                          QDate date) const {
  if (!hasVideo(date)) {
    return QCalendarWidget::paintCell(painter, rect, date);
  }
  painter->save();
  painter->setRenderHint(QPainter::Antialiasing);
  const QRect tile = rect.adjusted(2, 2, -2, -2);
  painter->setPen(Qt::NoPen);
  if (date == selectedDate()) {
    painter->setBrush(QColor("#193d32"));
    painter->drawRoundedRect(tile, 5, 5);
  }
  painter->setPen(QColor("#56d6a0"));
  painter->drawText(rect.adjusted(0, -2, 0, -2), Qt::AlignCenter, QString::number(date.day()));
  painter->setPen(Qt::NoPen);
  painter->setBrush(QColor("#56d6a0"));
  painter->drawEllipse(QPointF(rect.center().x(), rect.bottom() - 4), 1.5, 1.5);
  painter->restore();
}

bool XCalendar::hasVideo(const QDate &date) const {
  return mdate_.find(date) != mdate_.end();
}
