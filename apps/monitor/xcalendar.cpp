#include "xcalendar.h"
#include <Qpainter>

XCalendar::XCalendar(QWidget *parent) : QCalendarWidget(parent) {}

void XCalendar::paintCell(QPainter *painter, const QRect &rect,
                          QDate date) const {
  if (!hasVideo(date)) {
    return QCalendarWidget::paintCell(painter, rect, date);
  }
  auto font = painter->font();
  font.setBold(true);
  painter->save();
  painter->setPen(QColor(255, 0, 0));
  painter->setFont(font);
  if (date == selectedDate()) {
    painter->setBrush(QColor(118, 178, 224));
    painter->drawRect(rect);
  }
  painter->drawText(rect, Qt::AlignCenter, QString::number(date.day()));
  painter->restore();
}

bool XCalendar::hasVideo(const QDate &date) const {
  return mdate_.find(date) != mdate_.end();
}
