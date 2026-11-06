#ifndef XCALENDAR_H
#define XCALENDAR_H

#include <QCalendarWidget>
#include <set>

class XCalendar : public QCalendarWidget {
  Q_OBJECT
public:
  XCalendar(QWidget *parent = nullptr);
  void paintCell(QPainter *painter, const QRect &rect,
                 QDate date) const override;
  void AddDate(QDate date) { mdate_.insert(date); updateCells(); }
  void ClearDate() { mdate_.clear(); updateCells(); }

private:
  bool hasVideo(const QDate &date) const;

private:
  std::set<QDate> mdate_;
};

#endif // XCALENDAR_H
