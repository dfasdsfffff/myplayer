#ifndef XCALENDAR_H
#define XCALENDAR_H

#include <QCalendarWidget>

class XCalendar : public QCalendarWidget {
  Q_OBJECT
public:
  XCalendar(QWidget *parent = nullptr);
  void paintCell(QPainter *painter, const QRect &rect,
                 QDate date) const override;
  void AddDate(QDate date) { mdate_.insert(date); }
  void ClearDate() { mdate_.clear(); }

private:
  bool hasVideo(const QDate &date) const;

private:
  std::set<QDate> mdate_;
};

#endif // XCALENDAR_H
