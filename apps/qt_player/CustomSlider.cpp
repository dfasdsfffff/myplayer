#include "CustomSlider.h"
#include "globalhelper.h"
#include <QPainter>
#include <QEnterEvent>

CustomSlider::CustomSlider(QWidget *parent)
    : QSlider(parent)
{
    this->setMaximum(MAX_SLIDER_VALUE);
}

CustomSlider::~CustomSlider()
{
}

void CustomSlider::mousePressEvent(QMouseEvent *ev)
{
    //注意应先调用父类的鼠标点击处理事件，这样可以不影响拖动的情况
    QSlider::mousePressEvent(ev);
    //获取鼠标的位置，这里并不能直接从ev中取值（因为如果是拖动的话，鼠标开始点击的位置没有意义了）
    double pos = ev->pos().x() / (double)width();
    setValue(pos * (maximum() - minimum()) + minimum());

    emit SigCustomSliderValueChanged();
    mIsPressed = true;
}

void CustomSlider::mouseReleaseEvent(QMouseEvent *ev)
{
    QSlider::mouseReleaseEvent(ev);

    //emit SigCustomSliderValueChanged();
    mIsPressed = false;
}

void CustomSlider::mouseMoveEvent(QMouseEvent *ev)
{
    QSlider::mouseMoveEvent(ev);
    if (mIsPressed)
    {
        //获取鼠标的位置，这里并不能直接从ev中取值（因为如果是拖动的话，鼠标开始点击的位置没有意义了）
        double pos = ev->pos().x() / (double)width();
        setValue(pos * (maximum() - minimum()) + minimum());

        emit SigCustomSliderValueChanged();
    }
}

void CustomSlider::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool volume = objectName() == QStringLiteral("VolumeSlider");
    const qreal radius = volume ? 4 : (mIsPressed ? 7.5 : 5.5);
    const qreal inset = volume ? 4 : 7.5;
    const qreal span = qMax<qreal>(0, width() - 2 * inset);
    const qreal fraction = maximum() > minimum() ? qreal(value() - minimum()) / (maximum() - minimum()) : 0;
    const qreal x = inset + fraction * span;
    const qreal y = height() / 2.0;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#2B3945"));
    painter.drawRoundedRect(QRectF(inset, y - 2, span, 4), 2, 2);
    painter.setBrush(QColor(!isEnabled() ? "#465462" : volume ? "#9BAABB" : "#56D6A0"));
    painter.drawRoundedRect(QRectF(inset, y - 2, fraction * span, 4), 2, 2);
    if (isEnabled() && (volume || underMouse() || mIsPressed || hasFocus())) {
        painter.setBrush(QColor(volume ? "#EDF2F7" : mIsPressed ? "#8CE9BC" : "#6FE0AF"));
        if (mIsPressed) painter.setPen(QPen(QColor("#286447"), 2));
        painter.drawEllipse(QPointF(x, y), radius, radius);
    }
}

void CustomSlider::enterEvent(QEnterEvent* event)
{
    QSlider::enterEvent(event);
    update();
}

void CustomSlider::leaveEvent(QEvent* event)
{
    QSlider::leaveEvent(event);
    update();
}
