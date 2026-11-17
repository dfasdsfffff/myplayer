#include "review_window.h"
#include "review_timeline.h"
#include <player_sdk_qt/video_widget.h>
#include <QLineEdit>
#include <QListWidget>
#include <QDockWidget>
#include <QKeyEvent>
#include <QDialog>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QLabel>
#include <QSlider>
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
    review::ReviewWindow window(nullptr,temp.filePath(QStringLiteral("settings.ini")));
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
    ok &= expect(!panel->isVisible(), "notes panel can be hidden");
    notes->trigger();
    ok &= expect(panel->isVisible(), "notes panel can be restored");
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
    // Exercise actual SDK-backed range transitions, not only the controller model.
    std::cerr << "TEST: range begin" << std::endl;
    action("setInPointAction")->trigger();
    for (int i=0;i<4;++i) {
        steppedPosition=-1; nextFrame->trigger();
        ok &= expect(waitFor([&]{return steppedPosition>=0 && nextFrame->isEnabled();}),"prepare range out frame");
    }
    action("setOutPointAction")->trigger();
    std::cerr << "TEST: range ready" << std::endl;
    int rangeSeeks=0;
    QObject::connect(player,&player_sdk_qt::QtPlayer::operationFinished,&window,[&](const play_core::OperationResult& r){
        if (r.status==play_core::OperationStatus::Succeeded && r.requestedPositionMs && *r.requestedPositionMs==0) ++rangeSeeks;
    });
    loop->setChecked(true); action("togglePlaybackAction")->trigger();
    ok &= expect(waitFor([&]{return rangeSeeks>=2;}),"range loop seeks to in repeatedly");
    action("togglePlaybackAction")->trigger(); loop->setChecked(false);
    ok &= expect(waitFor([&]{return player->state()==play_core::PlayerState::Paused;}),"range playback can be paused");
    action("resetInPointAction")->trigger(); action("resetOutPointAction")->trigger();
    std::cerr << "TEST: range finished" << std::endl;
    // Existing document workflows remain functional after workspace extraction.
    auto* table=window.findChild<QTableWidget*>(QStringLiteral("notesTable"));
    auto fillNote=[&](const QString& text) {
        QTimer::singleShot(0,&window,[&,text]{
            auto* dialog=window.findChild<QDialog*>(QStringLiteral("noteDialog"));
            if (!dialog) return;
            dialog->findChild<QLineEdit*>(QStringLiteral("noteTimeEdit"))->setText(QStringLiteral("00:00:00.200"));
            dialog->findChild<QPlainTextEdit*>(QStringLiteral("noteTextEdit"))->setPlainText(text);
            dialog->accept();
        });
    };
    std::cerr << "TEST: note add" << std::endl;
    fillNote(QStringLiteral("中文审片，第一条")); action("addNoteAction")->trigger();
    ok &= expect(table->rowCount()==1 && table->item(0,2)->text().contains(QStringLiteral("第一条")),"add note in dock");
    fillNote(QStringLiteral("已编辑标注")); action("editNoteAction")->trigger();
    ok &= expect(table->item(0,2)->text()==QStringLiteral("已编辑标注"),"edit note in dock");
    action("undoAction")->trigger();
    ok &= expect(table->item(0,2)->text().contains(QStringLiteral("第一条")),"undo note edit");
    action("redoAction")->trigger();
    auto* filter=window.findChild<QLineEdit*>(QStringLiteral("noteSearchEdit"));
    filter->setText(QStringLiteral("不存在")); ok &= expect(table->rowCount()==0,"filter hides note");
    filter->clear(); table->selectRow(0); action("deleteNoteAction")->trigger();
    ok &= expect(table->rowCount()==0,"delete selected note"); action("undoAction")->trigger();
    ok &= expect(table->rowCount()==1,"undo note delete");
    std::cerr << "TEST: notes edited" << std::endl;
    const QString project=temp.filePath(QStringLiteral("workspace.review.json"));
    QTimer::singleShot(0,&window,[&]{
        if (auto* dialog=window.findChild<QFileDialog*>(QStringLiteral("saveProjectDialog"))) {
            dialog->selectFile(project); QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
        }
    });
    action("saveProjectAction")->trigger();
    review::ReviewDocument saved; QString saveError;
    ok &= expect(review::loadDocument(project,saved,saveError) && saved.notes.size()==1 && !window.isWindowModified(),"save v1 project after workspace edits");
    std::cerr << "TEST: notes saved" << std::endl;
    auto* video=window.findChild<player_sdk_qt::VideoWidget*>();
    auto* timeline=window.findChild<review::ReviewTimeline*>();
    ok &= expect(video && timeline,"workspace components available");
    action("actualSizeAction")->trigger();
    ok &= expect(!video->isViewFit() && video->viewScale()==1,"1:1 action uses source pixels");
    action("zoomInAction")->trigger();
    ok &= expect(video->viewScale()>1,"view zoom action");
    action("mirrorHorizontalAction")->trigger();
    ok &= expect(video->mirrorHorizontal(),"mirror action changes renderer");
    action("fitViewAction")->trigger();
    ok &= expect(video->isViewFit(),"fit action resets view");
    timeline->zoomAt(3,timeline->width()/2.0);
    ok &= expect(timeline->visibleSpan()<3000,"timeline zooms independently");
    auto* dock=window.findChild<QDockWidget*>();
    const bool wasVisible=dock->isVisible();
    action("presentationAction")->trigger();
    ok &= expect(window.isFullScreen() && !dock->isVisible(),"presentation hides dock");
    action("presentationAction")->trigger();
    ok &= expect(dock->isVisible()==wasVisible,"presentation restores dock");
    auto* search=window.findChild<QLineEdit*>(QStringLiteral("noteSearchEdit"));
    search->setFocus();
    QKeyEvent overrideKey(QEvent::ShortcutOverride,Qt::Key_I,Qt::NoModifier,QStringLiteral("i"));
    QApplication::sendEvent(search,&overrideKey);
    ok &= expect(overrideKey.isAccepted(),"text focus consumes media shortcut");
    const QString secondProject = temp.filePath(QStringLiteral("second.review.json"));
    auto secondDocument = saved;
    secondDocument.notes[0].text = QStringLiteral("独立素材标注");
    ok &= expect(review::saveDocument(secondProject, secondDocument, saveError), "save second asset fixture");
    ok &= expect(window.mediaCount() == 1 && window.addMedia(secondProject) && window.mediaCount() == 2,
                 "asset list registers current and additional projects");
    ok &= expect(window.addMedia(secondProject) && window.mediaCount() == 2, "asset list deduplicates projects");
    ok &= expect(!window.addMedia(temp.filePath(QStringLiteral("absent.mp4")))
                 && !window.activateMedia(-1) && !window.compareMedia(99), "invalid asset inputs are rejected");
    ok &= expect(window.activateMedia(1), "switch primary asset");
    ok &= expect(table->rowCount() == 1 && table->item(0,2)->text() == QStringLiteral("独立素材标注"),
                 "primary switch loads independent annotations");
    ok &= expect(waitFor([&]{ return jump->isEnabled(); }), "second primary opens");
    ok &= expect(window.activateMedia(0), "switch back to saved asset");
    ok &= expect(table->rowCount() == 1 && table->item(0,2)->text() == QStringLiteral("已编辑标注"),
                 "switch back retains saved annotations");
    ok &= expect(waitFor([&]{ return jump->isEnabled(); }), "original primary reopens");
    ok &= expect(window.compareMedia(1), "assign B from asset list");
    auto* compareMode = window.findChild<QComboBox*>(QStringLiteral("compareModeCombo"));
    ok &= expect(compareMode && compareMode->currentIndex() == 1, "B assignment enables side by side");
    for (int mode = 2; mode <= 4; ++mode) { compareMode->setCurrentIndex(mode); QApplication::processEvents(); }
    compareMode->setCurrentIndex(0);
    fillNote(QStringLiteral("取消切换测试")); action("addNoteAction")->trigger();
    QTimer::singleShot(0, &window, [&] {
        if (auto* dialog = window.findChild<QMessageBox*>(QStringLiteral("unsavedChangesDialog")))
            dialog->done(QMessageBox::Cancel);
    });
    ok &= expect(!window.activateMedia(1) && table->rowCount() == 2, "cancel primary switch preserves unsaved notes");
    action("undoAction")->trigger();
    auto* mediaList = window.findChild<QListWidget*>(QStringLiteral("reviewMediaList"));
    mediaList->setCurrentRow(1); action("removeMediaAction")->trigger();
    ok &= expect(window.mediaCount() == 1 && table->rowCount() == 1, "remove B leaves primary annotations intact");
    // Workspace state is separate from the portable review document.
    window.findChild<QSlider*>(QStringLiteral("volumeSlider"))->setValue(37);
    speed->setCurrentIndex(speed->findData(0.5));
    action("followTimelineAction")->setChecked(false);
    notes->setChecked(false);
    ok &= expect(window.close(),"clean window closes and persists preferences");
    review::ReviewWindow restored(nullptr,temp.filePath(QStringLiteral("settings.ini")));
    restored.show(); QApplication::processEvents();
    ok &= expect(restored.findChild<QSlider*>(QStringLiteral("volumeSlider"))->value()==37
        && restored.findChild<QComboBox*>(QStringLiteral("speedCombo"))->currentData().toDouble()==0.5
        && !restored.findChild<QAction*>(QStringLiteral("followTimelineAction"))->isChecked()
        && !restored.findChild<QDockWidget*>()->isVisible(),"workspace preferences restore independently");
    const QString missing=temp.filePath(QStringLiteral("missing.review.json"));
    saved.mediaPath=temp.filePath(QStringLiteral("missing.avi"));
    ok &= expect(review::saveDocument(missing,saved,saveError) && restored.openProject(missing),"missing media project opens");
    auto restoredAction=[&](const char* name){return restored.findChild<QAction*>(QString::fromLatin1(name));};
    ok &= expect(!restoredAction("setInPointAction")->isEnabled()
        && restored.findChild<QTableWidget*>(QStringLiteral("notesTable"))->rowCount()==1,"missing media retains notes but disables ranges");
    QTimer::singleShot(0,&restored,[&]{
        if(auto* dialog=restored.findChild<QFileDialog*>(QStringLiteral("relinkMediaDialog"))) {
            dialog->selectFile(QString::fromStdWString(fixtures.video.wstring()));
            QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
        }
    });
    restoredAction("relinkMediaAction")->trigger();
    ok &= expect(waitFor([&]{return restoredAction("setInPointAction")->isEnabled();}),"relink restores playback controls");
    restoredAction("undoAction")->trigger();
    ok &= expect(!restoredAction("setInPointAction")->isEnabled(),"undo relink returns to missing media safely");
    restored.close();
    std::cerr << "TEST: all scenarios finished" << std::endl;
    return ok ? 0 : 1;
}
