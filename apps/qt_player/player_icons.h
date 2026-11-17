#pragma once
#include <QIcon>
#include <QPainter>
#include <QPushButton>

// Scalable outline icons on a 24-unit grid, matching myplayer.op.
namespace PlayerIcons {
enum class Symbol { Play, Pause, Stop, Previous, Next, Volume, Mute, List, Settings,
                    Fullscreen, Menu, Minimize, Maximize, Restore, Close, Chevron, Repeat };
inline QIcon icon(Symbol symbol, bool primary = false)
{
    QIcon result;
    for (const auto mode : {QIcon::Normal, QIcon::Active, QIcon::Disabled, QIcon::Selected}) {
        for (int scale : {1, 2, 3}) {
            QPixmap pixmap(24 * scale, 24 * scale);
            pixmap.setDevicePixelRatio(scale);
            pixmap.fill(Qt::transparent);
            QPainter p(&pixmap);
            p.setRenderHint(QPainter::Antialiasing);
            const QColor color(mode == QIcon::Disabled ? "#4E5F70" : primary ? "#10251D" :
                (mode == QIcon::Active || mode == QIcon::Selected) ? "#6FE0AF" : "#9BAABB");
            p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            auto line = [&p](int x1, int y1, int x2, int y2) { p.drawLine(x1, y1, x2, y2); };
            auto triangle = [&p](int a, int b, int c) {
                QPolygon points; points << QPoint(a, 5) << QPoint(b, 12) << QPoint(c, 19);
                p.drawPolygon(points);
            };
            switch (symbol) {
            case Symbol::Play: triangle(8, 18, 8); break;
            case Symbol::Pause: p.drawRoundedRect(QRectF(6, 5, 4, 14), 1, 1); p.drawRoundedRect(QRectF(14, 5, 4, 14), 1, 1); break;
            case Symbol::Stop: p.drawRoundedRect(QRectF(5, 5, 14, 14), 2, 2); break;
            case Symbol::Previous: triangle(18, 8, 18); line(5, 5, 5, 19); break;
            case Symbol::Next: triangle(6, 16, 6); line(19, 5, 19, 19); break;
            case Symbol::Volume:
            case Symbol::Mute: {
                QPolygon speaker; speaker << QPoint(3, 9) << QPoint(7, 9) << QPoint(12, 5) << QPoint(12, 19) << QPoint(7, 15) << QPoint(3, 15);
                p.drawPolygon(speaker);
                if (symbol == Symbol::Mute) { line(16, 9, 21, 15); line(21, 9, 16, 15); }
                else { p.drawArc(QRectF(11, 7, 8, 10), -70*16, 140*16); p.drawArc(QRectF(9, 3, 14, 18), -60*16, 120*16); }
                break;
            }
            case Symbol::List: for (int y : {6, 12, 18}) { line(4, y, 5, y); line(9, y, 20, y); } break;
            case Symbol::Menu: for (int y : {6, 12, 18}) line(5, y, 19, y); break;
            case Symbol::Settings:
                p.drawEllipse(QRectF(6, 6, 12, 12)); p.drawEllipse(QRectF(10, 10, 4, 4));
                p.translate(12, 12); for (int i=0; i<8; ++i) { p.drawLine(QPointF(0, -6), QPointF(0, -9)); p.rotate(45); } break;
            case Symbol::Fullscreen:
                line(4, 9, 4, 4); line(4, 4, 9, 4); line(15, 4, 20, 4); line(20, 4, 20, 9);
                line(4, 15, 4, 20); line(4, 20, 9, 20); line(15, 20, 20, 20); line(20, 20, 20, 15); break;
            case Symbol::Minimize: line(6, 12, 18, 12); break;
            case Symbol::Maximize: p.drawRoundedRect(QRectF(5, 5, 14, 14), 1, 1); break;
            case Symbol::Restore: p.drawRect(QRectF(4, 8, 12, 12)); line(8, 4, 20, 4); line(20, 4, 20, 16); break;
            case Symbol::Close: line(7, 7, 17, 17); line(17, 7, 7, 17); break;
            case Symbol::Chevron: line(9, 6, 15, 12); line(15, 12, 9, 18); break;
            case Symbol::Repeat:
                line(4, 10, 4, 6); line(4, 6, 20, 6); line(16, 2, 20, 6); line(20, 6, 16, 10);
                line(20, 14, 20, 18); line(20, 18, 4, 18); line(8, 14, 4, 18); line(4, 18, 8, 22); break;
            }
            p.end();
            result.addPixmap(pixmap, mode);
            if (mode == QIcon::Selected) result.addPixmap(pixmap, QIcon::Normal, QIcon::On);
        }
    }
    return result;
}
inline void set(QPushButton* button, Symbol symbol, bool primary = false)
{
    const char* name = "";
    switch (symbol) {
    case Symbol::Play: name = "播放"; break;
    case Symbol::Pause: name = "暂停"; break;
    case Symbol::Stop: name = "停止"; break;
    case Symbol::Previous: name = "上一个"; break;
    case Symbol::Next: name = "下一个"; break;
    case Symbol::Volume: name = "静音"; break;
    case Symbol::Mute: name = "取消静音"; break;
    case Symbol::List: name = "播放列表"; break;
    case Symbol::Settings: name = "设置"; break;
    case Symbol::Fullscreen: name = "全屏"; break;
    case Symbol::Menu: name = "主菜单"; break;
    case Symbol::Minimize: name = "最小化"; break;
    case Symbol::Maximize: name = "最大化"; break;
    case Symbol::Restore: name = "还原"; break;
    case Symbol::Close: name = "关闭"; break;
    case Symbol::Chevron: name = "隐藏播放列表"; break;
    case Symbol::Repeat: name = "循环模式"; break;
    }
    button->setAccessibleName(QString::fromUtf8(name));
    button->setText({});
    button->setIcon(icon(symbol, primary));
    button->setIconSize(QSize(16, 16));
}
}
