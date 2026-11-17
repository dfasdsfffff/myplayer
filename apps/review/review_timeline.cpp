#include "review_timeline.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <algorithm>
#include <cmath>
namespace review {
namespace {
constexpr int Gutter = 16;
QColor markerColor(const QString& category) {
    if (category == QStringLiteral("问题")) return QColor("#e66767");
    if (category == QStringLiteral("建议")) return QColor("#3987e5");
    if (category == QStringLiteral("重点")) return QColor("#c98500");
    return QColor("#9085e9");
}
}
ReviewTimeline::ReviewTimeline(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(100); setFocusPolicy(Qt::StrongFocus); setMouseTracking(true);
    setAccessibleName(tr("毫秒审片时间线"));
}
void ReviewTimeline::setReviewNotes(const QList<ReviewNote>& notes, qint64 duration) {
    notes_ = notes; duration = std::max<qint64>(0, duration);
    if (duration_ != duration) { duration_ = duration; frameAll(); }
    update();
}
void ReviewTimeline::clampView() {
    const double total = static_cast<double>(std::max<qint64>(1, duration_));
    span_ = std::clamp(span_, std::min(100.0, total), total);
    start_ = std::clamp(start_, 0.0, total - span_); update();
}
void ReviewTimeline::frameAll() { start_ = 0; span_ = static_cast<double>(std::max<qint64>(1, duration_)); update(); }
qint64 ReviewTimeline::timeAt(double x) const {
    const double t = start_ + (x - Gutter) / std::max(1, width() - 2 * Gutter) * span_;
    return static_cast<qint64>(std::llround(std::clamp(t, 0.0, static_cast<double>(duration_))));
}
double ReviewTimeline::xAt(qint64 time) const {
    return Gutter + (static_cast<double>(time) - start_) / span_ * std::max(1, width() - 2 * Gutter);
}
void ReviewTimeline::zoomAt(double factor, double x) {
    if (!std::isfinite(factor) || factor <= 0 || duration_ <= 0) return;
    const double fraction = std::clamp((x - Gutter) / std::max(1, width() - 2 * Gutter), 0.0, 1.0);
    const double anchor = start_ + fraction * span_;
    span_ = std::clamp(span_ / factor, std::min(100.0, static_cast<double>(duration_)), static_cast<double>(duration_));
    start_ = anchor - fraction * span_; clampView();
}
void ReviewTimeline::panBy(double pixels) {
    if (!std::isfinite(pixels)) return;
    start_ -= pixels * span_ / std::max(1, width() - 2 * Gutter); clampView();
}
void ReviewTimeline::setPosition(qint64 position) {
    if (scrubbing_) return;
    position_ = std::clamp(position, qint64{0}, duration_);
    if (follow_ && !panning_ && (position_ < start_ || position_ > start_ + span_)) {
        start_ = static_cast<double>(position_) - span_ * 0.15; clampView();
    }
    update();
}
void ReviewTimeline::setPlaybackRange(qint64 in, qint64 out) { in_ = in; out_ = out; update(); }
int ReviewTimeline::noteAt(double x) const {
    int best = -1; double distance = 9;
    for (int i = 0; i < notes_.size(); ++i) {
        const double d = std::abs(xAt(notes_[i].positionMs) - x);
        if (d < distance) { distance = d; best = i; }
    }
    return best;
}
void ReviewTimeline::paintEvent(QPaintEvent*) {
    QPainter p(this); p.fillRect(rect(), QColor("#10171f"));
    p.setClipRect(QRect(Gutter, 0, std::max(1, width()-2*Gutter), height()));
    if (duration_ <= 0) { p.setPen(QColor("#aab7c4")); p.drawText(rect(), Qt::AlignCenter, tr("打开可定位的视频以显示时间线")); return; }
    if (out_ > in_) {
        const double left = std::clamp(xAt(in_), double(Gutter), double(width()-Gutter));
        const double right = std::clamp(xAt(out_), double(Gutter), double(width()-Gutter));
        p.fillRect(QRectF(left,42,std::max(0.0,right-left),17),QColor("#245948"));
        p.setPen(QColor("#cad5df"));
        if (xAt(in_) >= Gutter && xAt(in_) < width()-Gutter-20) p.drawText(QPointF(xAt(in_)+3,55),"I");
        if (xAt(out_) > Gutter+20 && xAt(out_) <= width()-Gutter) p.drawText(QPointF(xAt(out_)-12,55),"O");
    }
    const double desired = span_ * 110 / std::max(1,width()-2*Gutter);
    const double power = std::pow(10.0,std::floor(std::log10(std::max(1.0,desired))));
    double step=power;
    for (double n : {1.,2.,5.,10.}) if (n*power>=desired) { step=n*power; break; }
    for (double t=std::ceil(start_/step)*step; t<=start_+span_; t+=step) {
        const double x=xAt(static_cast<qint64>(t));
        p.setPen(QColor("#34424f")); p.drawLine(QPointF(x,24),QPointF(x,40));
        p.setPen(QColor("#aab7c4")); p.drawText(QRectF(x+3,2,108,20),Qt::AlignLeft,formatTime(static_cast<qint64>(t)));
    }
    for (int i=0; i<notes_.size(); ++i) {
        const auto& note=notes_[i]; const double x=xAt(note.positionMs);
        if (x<Gutter || x>width()-Gutter) continue;
        p.setPen(QPen(i==selected_ ? QColor("#ffffff") : QColor("#10171f"),2));
        p.setBrush(markerColor(note.category)); p.drawEllipse(QPointF(x,70),5,5);
    }
    const double playhead=xAt(position_);
    p.setPen(QPen(QColor("#f0f5fa"),2)); p.drawLine(QPointF(playhead,24),QPointF(playhead,79));
    p.setClipping(false);
    const QStringList categories{QStringLiteral("问题"),QStringLiteral("建议"),QStringLiteral("重点"),QStringLiteral("其他")};
    int legendX=16;
    for (const auto& category : categories) {
        p.setPen(Qt::NoPen); p.setBrush(markerColor(category)); p.drawEllipse(QPoint(legendX+4,90),4,4);
        p.setPen(QColor("#aab7c4")); p.drawText(legendX+13,94,category); legendX+=62;
    }
    if (hasFocus()) { p.setPen(QPen(QColor("#56d6a0"),1)); p.setBrush(Qt::NoBrush); p.drawRect(rect().adjusted(0,0,-1,-1)); }
}
void ReviewTimeline::mousePressEvent(QMouseEvent* e) {
    if (duration_<=0) return;
    setFocus(Qt::MouseFocusReason); lastX_=e->position().x();
    if (e->button()==Qt::MiddleButton) { panning_=true; e->accept(); return; }
    if (e->button()!=Qt::LeftButton) return;
    if (e->position().y()>=61 && e->position().y()<=78) {
        const int index=noteAt(lastX_); if (index>=0) { emit noteActivated(index); return; }
    }
    scrubbing_=true; position_=timeAt(lastX_); emit scrubStarted(); emit previewChanged(); update();
}
void ReviewTimeline::mouseMoveEvent(QMouseEvent* e) {
    if (panning_) panBy(e->position().x()-lastX_);
    if (scrubbing_) { position_=timeAt(e->position().x()); emit previewChanged(); update(); }
    lastX_=e->position().x();
}
void ReviewTimeline::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button()==Qt::MiddleButton) panning_=false;
    if (e->button()==Qt::LeftButton && scrubbing_) {
        position_=timeAt(e->position().x()); scrubbing_=false; update(); emit scrubFinished(position_);
    }
}
void ReviewTimeline::wheelEvent(QWheelEvent* e) { zoomAt(std::pow(1.2,e->angleDelta().y()/120.0),e->position().x()); e->accept(); }
void ReviewTimeline::keyPressEvent(QKeyEvent* e) {
    if (e->key()==Qt::Key_Backspace) frameAll();
    else if (e->key()==Qt::Key_Equal || e->key()==Qt::Key_Plus) zoomAt(1.5,width()/2.0);
    else if (e->key()==Qt::Key_Minus) zoomAt(1/1.5,width()/2.0);
    else { QWidget::keyPressEvent(e); return; }
    e->accept();
}
void ReviewTimeline::cancelScrub() {
    panning_=false; if (scrubbing_) { scrubbing_=false; emit scrubCancelled(); update(); }
}
void ReviewTimeline::focusOutEvent(QFocusEvent* e) { cancelScrub(); QWidget::focusOutEvent(e); }
bool ReviewTimeline::event(QEvent* e) {
    if (e->type()==QEvent::EnabledChange && !isEnabled()) cancelScrub();
    if (e->type()==QEvent::ToolTip) {
        auto* help=static_cast<QHelpEvent*>(e); const int i=noteAt(help->pos().x());
        if (i>=0) QToolTip::showText(help->globalPos(),formatTime(notes_[i].positionMs)+" · "+notes_[i].category+"\n"+notes_[i].text,this);
        else QToolTip::showText(help->globalPos(),formatTime(timeAt(help->pos().x())),this);
        return true;
    }
    return QWidget::event(e);
}
}
