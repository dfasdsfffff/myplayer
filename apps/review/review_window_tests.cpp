#include "review_window.h"
#include "media_fixture_builder.h"
#include <player_sdk_qt/qt_player.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QInputDialog>
#include <QFileDialog>
#include <QImage>
#include <QMessageBox>
#include <QPointer>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>

namespace {
bool waitFor(const std::function<bool()>& predicate, int timeout = 8000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout) {
        QApplication::processEvents();
        QThread::msleep(5);
    }
    return predicate();
}
bool expect(bool value, const char* message)
{
    if (!value) std::cerr << "FAILED: " << message << '\n';
    return value;
}
}

int main(int argc, char** argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("review-window-tests"));
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir temp;
    if (!temp.isValid()) return 1;
    const auto fixtures = BuildMediaFixtures(std::filesystem::path(temp.path().toStdWString()), 30);
    review::ReviewWindow window;
    window.show();
    auto action = [&window](const char* name) { return window.findChild<QAction*>(QString::fromLatin1(name)); };
    auto* jump = action("jumpToTimeAction");
    auto* start = action("goToStartAction");
    auto* end = action("goToEndAction");
    auto* info = action("mediaInfoAction");
    auto* frame = action("exportFrameAction");
    auto* loop = action("loopPlaybackAction");
    auto* notes = action("notesVisibleAction");
    auto* previousFrame = action("previousFrameAction");
    auto* nextFrame = action("nextFrameAction");
    if (!expect(previousFrame && nextFrame, "frame step actions exist")) return 1;
    if (!expect(jump && start && end && info && frame && loop && notes, "review actions exist")) return 1;
    bool ok = expect(!previousFrame->isEnabled() && !nextFrame->isEnabled(), "empty player disables frame stepping");
    ok &= expect(!jump->isEnabled() && !start->isEnabled() && !end->isEnabled()
                     && !info->isEnabled() && !frame->isEnabled(), "empty player disables media actions");
    auto* panel = window.findChild<QWidget*>(QStringLiteral("reviewNotesPanel"));
    notes->trigger();
    ok &= expect(panel->isHidden(), "notes panel can be hidden");
    notes->trigger();
    ok &= expect(!panel->isHidden(), "notes panel can be restored");
    auto* speed = window.findChild<QComboBox*>(QStringLiteral("speedCombo"));
    ok &= expect(speed && speed->findData(0.1) >= 0 && speed->findData(0.25) >= 0,
                 "slow playback presets available");
    loop->setChecked(true);
    if (!expect(window.openVideo(QString::fromStdWString(fixtures.video.wstring())), "open fixture")) return 1;
    QPointer<player_sdk_qt::QtPlayer> first = window.findChild<player_sdk_qt::QtPlayer*>();
    ok &= expect(waitFor([&] { return jump->isEnabled() && frame->isEnabled(); }), "playback enables navigation and frame export");
    ok &= expect(info->isEnabled(), "media information available");
    ok &= expect(waitFor([&] { return first.isNull(); }), "loop reopens media after EOF");
    loop->setChecked(false);
    ok &= expect(waitFor([&] { return jump->isEnabled(); }), "reopened media is controllable");
    auto* player = window.findChild<player_sdk_qt::QtPlayer*>();
    qint64 steppedPosition = -1;
    QObject::connect(player, &player_sdk_qt::QtPlayer::operationFinished, &window,
                     [&](const play_core::OperationResult& result) {
        if (result.status == play_core::OperationStatus::Succeeded && !result.requestedPositionMs && result.actualPositionMs)
            steppedPosition = *result.actualPositionMs;
    });
    ok &= expect(nextFrame->isEnabled() && previousFrame->isEnabled(), "video enables frame stepping");
    nextFrame->trigger();
    ok &= expect(!nextFrame->isEnabled(), "step action disables while request is pending");
    ok &= expect(waitFor([&] { return steppedPosition >= 0 && nextFrame->isEnabled(); }), "next frame completes");
    const qint64 nextPosition = steppedPosition;
    steppedPosition = -1;
    previousFrame->trigger();
    ok &= expect(waitFor([&] { return steppedPosition >= 0; }), "previous frame completes");
    ok &= expect(steppedPosition == std::max<qint64>(0, nextPosition - 100)
                 && player->state() == play_core::PlayerState::Paused, "review step uses adjacent frame and stays paused");
    qint64 requested = -1;
    QObject::connect(player, &player_sdk_qt::QtPlayer::operationFinished, &window,
                     [&](const play_core::OperationResult& result) {
        if (result.status == play_core::OperationStatus::Succeeded && result.requestedPositionMs)
            requested = *result.requestedPositionMs;
    });
    QTimer::singleShot(0, &window, [&] {
        if (auto* dialog = window.findChild<QInputDialog*>(QStringLiteral("jumpToTimeDialog"))) {
            dialog->setTextValue(QStringLiteral("00:00:00.100"));
            dialog->accept();
        }
    });
    jump->trigger();
    ok &= expect(waitFor([&] { return requested == 100; }), "time dialog submits requested millisecond position");
    requested = -1;
    start->trigger();
    ok &= expect(waitFor([&] { return requested == 0; }), "home seeks to start");
    bool rejected = false;
    QTimer::singleShot(0, &window, [&] {
        if (auto* dialog = window.findChild<QInputDialog*>(QStringLiteral("jumpToTimeDialog"))) {
            dialog->setTextValue(QStringLiteral("99:00:00.000"));
            dialog->accept();
            QTimer::singleShot(0, &window, [&] {
                if (auto* error = window.findChild<QMessageBox*>(QStringLiteral("reviewErrorDialog"))) {
                    rejected = true;
                    error->accept();
                }
            });
        }
    });
    jump->trigger();
    ok &= expect(rejected && requested == 0, "out-of-range time is rejected without seeking");
    const QString png = temp.filePath(QStringLiteral("frame.png"));
    QTimer::singleShot(0, &window, [&] {
        if (auto* dialog = window.findChild<QFileDialog*>(QStringLiteral("exportFrameDialog"))) {
            dialog->selectFile(png);
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
        }
    });
    frame->trigger();
    const QImage exported(png);
    ok &= expect(!exported.isNull() && exported.width() > 0 && exported.height() > 0,
                 "current viewport exports readable PNG");
    return ok ? 0 : 1;
}
