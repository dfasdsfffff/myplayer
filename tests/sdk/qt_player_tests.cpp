#include <player_sdk_qt/qt_player.h>
#include <player_sdk_qt/video_widget.h>
#include "media_fixture_builder.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <thread>
using namespace player_sdk_qt;
namespace {
template<class F> bool wait(F predicate, int timeout = 5000) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < timeout) { QApplication::processEvents(); QThread::msleep(2); }
    return predicate();
}
void require(bool b,const char* msg) { if (!b) throw std::runtime_error(msg); }
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        QTemporaryDir dir; require(dir.isValid(),"No temporary directory");
        const auto fixtures = BuildMediaFixtures(std::filesystem::path(dir.path().toStdWString()),120);
        const auto u8 = fixtures.video.u8string();
        auto* player = new QtPlayer;
        auto* view = new VideoWidget; view->resize(320,240); view->setPlayer(player); view->show();
        int frames = 0; int results = 0;
        QObject::connect(player,&QtPlayer::videoFrameChanged,&app,[&](auto f) { if (f) ++frames; });
        QObject::connect(player,&QtPlayer::operationFinished,&app,[&](auto) { ++results; });
        require(bool(player->open(play_core::MediaSource{std::string(u8.begin(),u8.end())})),"Open rejected");
        require(bool(player->play()),"Play rejected");
        require(wait([&] { return view->presentedFrames() > 1; }),"Qt widget did not present video");
        const int before = frames;
        // Deliberately stall the QObject thread while Core produces more frames.
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        QApplication::processEvents();
        require(frames-before <= 2,"Qt queued individual frames during a UI stall");
        int accepted = 0;
        for (int i=0;i<256;++i) if (player->setVolume(.5)) ++accepted;
        require(accepted <= 128,"Qt terminal result budget not enforced");
        require(wait([&] { return results >= accepted+2; }),"Qt terminal results lost");
        const QString subtitle = dir.filePath("字幕.srt"); QFile file(subtitle);
        require(file.open(QIODevice::WriteOnly),"Subtitle fixture write failed");
        file.write("1\n00:00:00,000 --> 00:00:05,000\nSDK subtitle\n"); file.close();
        require(view->loadExternalSubtitle(subtitle),"SRT subtitle failed");
        require(view->hasExternalSubtitle(),"Subtitle not retained");
        QApplication::processEvents();
        const auto shot = view->grab(); require(!shot.isNull(),"Video widget screenshot is empty");
        const auto screenshotPath = qEnvironmentVariable("PLAYER_SDK_TEST_SCREENSHOT");
        if (!screenshotPath.isEmpty()) require(shot.save(screenshotPath),"Screenshot save failed");
        view->clearExternalSubtitle();
        delete player; require(view->player() == nullptr,"Player destruction left stale view binding");
        delete view;
        QtPlayer other; auto* first = new VideoWidget; auto* second = new VideoWidget;
        first->setPlayer(&other); second->setPlayer(&other);
        require(first->player() == nullptr && second->player() == &other,"Single view binding not enforced");
        delete first; delete second;
        other.close(); QApplication::processEvents();
        std::cout << "Qt video, bounded delivery, subtitles and destruction order passed\n"; return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
