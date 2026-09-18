#include "mainwid.h"
#include "ctrlbar.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QScreen>
#include <QPushButton>
#include <QSlider>
#include <QStandardPaths>
#include <QTimer>

#include <iostream>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

QMenu* RootMenu(MainWid& window)
{
    const QList<QMenu*> menus = window.findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly);
    for (QMenu* menu : menus)
    {
        if (menu->title().isEmpty() && !menu->actions().isEmpty())
            return menu;
    }
    return nullptr;
}

QAction* FindTopLevelAction(QMenu* menu, const QString& text)
{
    for (QAction* action : menu->actions())
    {
        if (action->text().section('\t', 0, 0) == text)
            return action;
    }
    return nullptr;
}

} // namespace

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
#endif
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    const QString configFile = QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath("player_config.ini");
    QFile::remove(configFile);

    MainWid window;
    if (!Expect(window.Init(), "main window initializes offscreen"))
        return 1;
    window.show();
    app.processEvents();

    QMenu* menu = RootMenu(window);
    if (!Expect(menu, "main window exposes its context menu"))
        return 1;

    int audioMenus = 0;
    int subtitleMenus = 0;
    for (QAction* action : menu->actions())
    {
        if (action->menu())
        {
            if (!Expect(!action->menu()->actions().isEmpty(), "top-level submenus are non-empty"))
                return 1;
        }
        else if (!Expect(action->isEnabled(), "top-level leaf actions are enabled"))
        {
            return 1;
        }

        const QString text = action->text().section('\t', 0, 0);
        audioMenus += text == QStringLiteral("音轨");
        subtitleMenus += text == QStringLiteral("字幕");
    }
    if (!Expect(audioMenus == 1 && subtitleMenus == 1, "audio and subtitle menus appear exactly once"))
        return 1;

    auto* ctrlBarState = window.findChild<CtrlBar*>(QStringLiteral("CtrlBarWid"));
    auto* playSlider = window.findChild<QSlider*>(QStringLiteral("PlaySlider"));
    auto* cycleButton = window.findChild<QPushButton*>(QStringLiteral("CycleSettingBtn"));
    if (!Expect(ctrlBarState && playSlider && cycleButton, "control-bar state is observable"))
        return 1;
    ctrlBarState->OnVideoPlaySeconds(5);
    if (!Expect(playSlider->value() == 0, "play time before duration leaves progress at zero"))
        return 1;
    cycleButton->click();
    if (!Expect(cycleButton->toolTip() == QStringLiteral("循环模式：随机播放"),
            "random loop mode has the correct tooltip"))
        return 1;

    auto* hideTimer = window.findChild<QTimer*>(QStringLiteral("ctrlBarHideTimer"));
    QAction* fullScreenAction = FindTopLevelAction(menu, QStringLiteral("全屏"));
    auto* ctrlBar = window.findChild<QWidget*>(QStringLiteral("CtrlBarWid"));
    auto* show = window.findChild<QWidget*>(QStringLiteral("ShowWid"));
    if (!Expect(hideTimer && fullScreenAction && ctrlBar && show,
            "full-screen controls and cancellable hide timer are discoverable"))
        return 1;

    fullScreenAction->trigger();
    QEvent leaveEvent(QEvent::Leave);
    QApplication::sendEvent(show, &leaveEvent);
    if (!Expect(hideTimer->isActive(), "leaving the video schedules control-bar hiding"))
        return 1;

    QScreen* screen = QGuiApplication::primaryScreen();
    if (!Expect(screen, "a screen is available for full-screen controls"))
        return 1;
    const QRect screenGeometry = screen->geometry();
    const QPoint globalPosition(screenGeometry.center().x(),
        screenGeometry.bottom() - ctrlBar->height() / 2);
    const QPoint localPosition = ctrlBar->mapFromGlobal(globalPosition);
    QMouseEvent returnEvent(QEvent::MouseMove, localPosition, globalPosition,
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(ctrlBar, &returnEvent);
    if (!Expect(!hideTimer->isActive(), "returning to controls cancels pending hiding"))
        return 1;

    fullScreenAction->trigger();
    QFile::remove(configFile);
    return 0;
}
