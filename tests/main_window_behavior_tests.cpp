#define SDL_MAIN_HANDLED

#include "mainwid.h"
#include "ctrlbar.h"
#include "show.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QScreen>
#include <QPushButton>
#include <QSlider>
#include <QStandardPaths>
#include <QTemporaryDir>
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

QMenu* FindSubMenu(QMenu* menu, const QString& text)
{
    QAction* action = FindTopLevelAction(menu, text);
    return action ? action->menu() : nullptr;
}

#ifdef Q_OS_WIN
bool CheckNativeResizeBorders(MainWid& window)
{
    if (QGuiApplication::platformName() != QStringLiteral("windows"))
        return true;
    window.showNormal();
    window.setGeometry(137, 91, 900, 650);
    QApplication::processEvents();
    const HWND handle = reinterpret_cast<HWND>(window.winId());
    RECT frame;
    if (!Expect(GetWindowRect(handle, &frame), "native window geometry is available"))
        return false;
    const LONG centerX = (frame.left + frame.right) / 2;
    const LONG centerY = (frame.top + frame.bottom) / 2;
    const struct {
        LONG x;
        LONG y;
        LRESULT expected;
    } probes[] = {
        {frame.left + 1, centerY, HTLEFT},
        {frame.right - 2, centerY, HTRIGHT},
        {centerX, frame.top + 1, HTTOP},
        {centerX, frame.bottom - 2, HTBOTTOM},
        {frame.left + 1, frame.top + 1, HTTOPLEFT},
        {frame.right - 2, frame.top + 1, HTTOPRIGHT},
        {frame.left + 1, frame.bottom - 2, HTBOTTOMLEFT},
        {frame.right - 2, frame.bottom - 2, HTBOTTOMRIGHT},
        {centerX, centerY, HTCLIENT},
    };
    for (const auto& probe : probes) {
        const LRESULT hit = SendMessageW(handle, WM_NCHITTEST, 0, MAKELPARAM(probe.x, probe.y));
        if (!Expect(hit == probe.expected, "native physical-pixel hit testing identifies every resize edge"))
            return false;
    }
    return true;
}
#endif

} // namespace

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
#endif
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
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
    window.activateWindow();
    window.setFocus();
    app.processEvents();
#ifdef Q_OS_WIN
    if (!CheckNativeResizeBorders(window))
        return 1;
#endif

    bool openDialogShown = false;
    QTimer::singleShot(0, &window, [&]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (auto* dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) {
                openDialogShown = true;
                dialog->reject();
            }
        }
    });
    QKeyEvent openKey(QEvent::KeyPress, Qt::Key_O, Qt::ControlModifier);
    QApplication::sendEvent(&window, &openKey);
    QKeyEvent openKeyRelease(QEvent::KeyRelease, Qt::Key_O, Qt::ControlModifier);
    QApplication::sendEvent(&window, &openKeyRelease);
    app.processEvents();
    if (!Expect(openDialogShown, "Ctrl+O opens a file dialog while the popup menu is closed"))
        return 1;

    window.activateWindow();
    window.setFocus();
    app.processEvents();
    auto* fullscreenSurface = window.findChild<Show*>(QStringLiteral("ShowWid"));
    if (!Expect(fullscreenSurface, "fullscreen playback surface is available"))
        return 1;
    QKeyEvent fullscreenPress(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QKeyEvent fullscreenRelease(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(&window, &fullscreenPress);
    QApplication::sendEvent(&window, &fullscreenRelease);
    if (!Expect(fullscreenSurface->isFullScreen(), "one Enter key cycle enters fullscreen exactly once"))
        return 1;
    fullscreenSurface->activateWindow();
    app.processEvents();
    QApplication::sendEvent(fullscreenSurface, &fullscreenPress);
    QApplication::sendEvent(fullscreenSurface, &fullscreenRelease);
    if (!Expect(!fullscreenSurface->isFullScreen(), "a second Enter key cycle leaves fullscreen"))
        return 1;

    QMenu* menu = RootMenu(window);
    if (!Expect(menu, "main window exposes its context menu"))
        return 1;

    QMenu* openMenu = FindSubMenu(menu, QStringLiteral("打开"));
    if (!Expect(openMenu && FindTopLevelAction(openMenu, QStringLiteral("加载字幕...")) &&
                    FindTopLevelAction(openMenu, QStringLiteral("卸载字幕")),
            "open menu exposes subtitle load and unload actions"))
        return 1;

    int audioMenus = 0;
    int subtitleMenus = 0;
    for (QAction* action : menu->actions())
    {
        if (action->menu())
        {
            const QString text = action->text().section('\t', 0, 0);
            if (!Expect(!action->menu()->actions().isEmpty() || text == QStringLiteral("音轨") || text == QStringLiteral("字幕"),
                    "non-track top-level submenus are non-empty"))
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

    auto* bridge = window.findChild<PlaybackRuntimeBridge*>();
    QMenu* audioMenu = FindSubMenu(menu, QStringLiteral("音轨"));
    QMenu* subtitleMenu = FindSubMenu(menu, QStringLiteral("字幕"));
    if (!Expect(bridge && audioMenu && subtitleMenu, "track menus receive runtime media information"))
        return 1;

    MediaInfo mediaInfo;
    mediaInfo.tracks = {
        TrackInfo{3, AVMEDIA_TYPE_AUDIO, "eng", "English", "aac", true, false},
        TrackInfo{8, AVMEDIA_TYPE_SUBTITLE, "zho", "中文字幕", "ass", false, false},
    };
    bridge->SigMediaInfo(mediaInfo);
    app.processEvents();
    if (!Expect(audioMenu->actions().size() == 1 && audioMenu->actions().front()->isCheckable() &&
                    audioMenu->actions().front()->isChecked() && audioMenu->actions().front()->data() == 3,
                "audio menu exposes checked stream-index action") ||
        !Expect(subtitleMenu->actions().size() == 2 && subtitleMenu->actions().front()->text() == QStringLiteral("关闭字幕") &&
                    subtitleMenu->actions().at(1)->text() == QStringLiteral("zho - 中文字幕 (ass)") &&
                    subtitleMenu->actions().at(1)->isCheckable() && subtitleMenu->actions().at(1)->data() == 8,
                "subtitle menu exposes disable and stream-index actions"))
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

    QTemporaryDir subtitleDir;
    const QString subtitlePath = subtitleDir.filePath(QStringLiteral("old.srt"));
    QFile subtitleFile(subtitlePath);
    if (!Expect(subtitleDir.isValid() && subtitleFile.open(QIODevice::WriteOnly),
            "subtitle fixture can be created")) return 1;
    subtitleFile.write("1\n00:00:00,000 --> 00:00:05,000\nOld subtitle\n");
    subtitleFile.close();
    Show isolatedShow(nullptr);
    if (!Expect(isolatedShow.LoadExternalSubtitleFile(subtitlePath), "external subtitle loads")) return 1;
    isolatedShow.OnPlay(QStringLiteral("next.mp4"));
    if (!Expect(!isolatedShow.HasExternalSubtitle(), "new media clears the previous external subtitle")) return 1;

    isolatedShow.show();
    app.processEvents();
    auto* audioLabel = isolatedShow.findChild<QLabel*>(QStringLiteral("label"));
    auto* videoView = isolatedShow.findChild<player_sdk_qt::VideoWidget*>();
    if (!Expect(audioLabel && videoView, "media display surfaces are available"))
        return 1;
    if (!Expect(!audioLabel->isVisible() && videoView->isVisible(),
            "video surface is not covered by the opaque audio placeholder"))
        return 1;
    isolatedShow.ShowAudioOnly(QStringLiteral("track.wav"));
    if (!Expect(audioLabel->isVisible() && !videoView->isVisible() &&
                    audioLabel->text().contains(QStringLiteral("track.wav")),
            "audio-only media shows its filename instead of the video surface"))
        return 1;
    isolatedShow.ClearAudioOnlyIndicator();
    if (!Expect(!audioLabel->isVisible() && videoView->isVisible(),
            "switching back to video removes the opaque audio placeholder"))
        return 1;
    isolatedShow.ShowAudioOnly(QStringLiteral("next.wav"));
    if (!Expect(audioLabel->isVisible() && !videoView->isVisible(),
            "audio placeholder can be shown again after video playback"))
        return 1;
    isolatedShow.OnStopFinished();
    if (!Expect(!audioLabel->isVisible() && videoView->isVisible(),
            "stopping audio restores the unobstructed video surface"))
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
