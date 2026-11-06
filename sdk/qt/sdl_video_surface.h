#pragma once
#include <QWidget>
#include <QPlatformSurfaceEvent>
#include <functional>

namespace player_sdk_qt {
// SDL owns drawing; Qt owns the native child window and its lifetime.
class SdlVideoSurface final : public QWidget {
public:
    explicit SdlVideoSurface(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_PaintOnScreen);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    std::function<void()> repaint;
    std::function<void()> releasing;
    QPaintEngine* paintEngine() const override { return nullptr; }
protected:
    void paintEvent(QPaintEvent*) override { if (repaint) repaint(); }
    bool event(QEvent* event) override {
        if (event->type() == QEvent::PlatformSurface &&
            static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
            if (releasing) releasing();
        return QWidget::event(event);
    }
};
}
