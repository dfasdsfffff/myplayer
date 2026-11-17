#pragma once
#include "review_document.h"
#include <QWidget>
namespace review {
class ReviewTimeline final : public QWidget {
    Q_OBJECT
public:
    explicit ReviewTimeline(QWidget* parent = nullptr);
    void setReviewNotes(const QList<ReviewNote>& notes, qint64 duration);
    void setPosition(qint64 position);
    qint64 position() const { return position_; }
    bool isScrubbing() const { return scrubbing_; }
    void cancelScrub();
    void setPlaybackRange(qint64 in, qint64 out);
    void setSelectedNote(int index) { selected_ = index; update(); }
    void setFollow(bool follow) { follow_ = follow; }
    bool follow() const { return follow_; }
    void frameAll();
    void zoomAt(double factor, double x);
    void panBy(double pixels);
    qint64 timeAt(double x) const;
    double xAt(qint64 time) const;
    double visibleStart() const { return start_; }
    double visibleSpan() const { return span_; }
signals:
    void scrubStarted();
    void scrubFinished(qint64 position);
    void scrubCancelled();
    void previewChanged();
    void noteActivated(int index);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    bool event(QEvent*) override;
private:
    void clampView();
    int noteAt(double x) const;
    qint64 duration_{0}, position_{0}, in_{0}, out_{0};
    double start_{0}, span_{1}, lastX_{0};
    bool scrubbing_{false}, panning_{false}, follow_{true};
    int selected_{-1};
    QList<ReviewNote> notes_;
};
}
