#include "review_settings.h"
#include <QSettings>
#include <QMainWindow>
#include <QGuiApplication>
#include <QScreen>
#include <memory>
#include <algorithm>
#include <cmath>
namespace review {
namespace {
std::unique_ptr<QSettings> store(const QString& path) {
    if (!path.isEmpty()) return std::make_unique<QSettings>(path,QSettings::IniFormat);
    return std::make_unique<QSettings>();
}
}
ReviewSettings ReviewSettings::load(const QString& file) {
    auto s=store(file); ReviewSettings value;
    s->beginGroup(QStringLiteral("reviewWorkspace/v1"));
    value.geometry=s->value(QStringLiteral("geometry")).toByteArray();
    value.layout=s->value(QStringLiteral("layout")).toByteArray();
    bool ok=false;
    const int volume=s->value(QStringLiteral("volume"),100).toInt(&ok);
    if (ok && volume>=0 && volume<=100) value.volume=volume;
    const double speed=s->value(QStringLiteral("speed"),1.0).toDouble(&ok);
    if (ok && std::isfinite(speed) && speed>=0.1 && speed<=2) value.speed=speed;
    const auto boolean=[&](const QString& key,bool fallback) {
        const QString text=s->value(key,fallback).toString().toLower();
        if (text==QStringLiteral("true") || text==QStringLiteral("1")) return true;
        if (text==QStringLiteral("false") || text==QStringLiteral("0")) return false;
        return fallback;
    };
    value.follow=boolean(QStringLiteral("follow"),true);
    value.stopOnScrub=boolean(QStringLiteral("stopOnScrub"),false);
    return value;
}
void ReviewSettings::save(const QString& file) const {
    auto s=store(file); s->beginGroup(QStringLiteral("reviewWorkspace/v1"));
    s->setValue(QStringLiteral("geometry"),geometry); s->setValue(QStringLiteral("layout"),layout);
    s->setValue(QStringLiteral("volume"),volume); s->setValue(QStringLiteral("speed"),speed);
    s->setValue(QStringLiteral("follow"),follow); s->setValue(QStringLiteral("stopOnScrub"),stopOnScrub);
    s->sync();
}
void ReviewSettings::restoreWindow(QMainWindow* window) const {
    if (!geometry.isEmpty()) window->restoreGeometry(geometry);
    const QByteArray original=window->saveState(1);
    if (!layout.isEmpty() && !window->restoreState(layout,1)) window->restoreState(original,1);
    bool visible=false;
    for (auto* screen : QGuiApplication::screens()) {
        const QRect intersection=screen->availableGeometry().intersected(window->frameGeometry());
        if (intersection.width()>=100 && intersection.height()>=100) visible=true;
    }
    if (!visible) if (auto* screen=QGuiApplication::primaryScreen()) {
        const QRect area=screen->availableGeometry();
        window->resize(std::min(1280,area.width()),std::min(820,area.height()));
        window->move(area.center()-window->rect().center());
    }
}
}
