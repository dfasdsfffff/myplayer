#include "review_timeline.h"
#include "review_playback_controller.h"
#include "review_settings.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QMainWindow>
#include <QDockWidget>
#include <QMouseEvent>
#include <iostream>
#include <vector>
#include <cmath>
#include <stdexcept>
using namespace review;
namespace {
void require(bool ok,const char* text) { if (!ok) throw std::runtime_error(text); }
void controllerTests() {
    using C=ReviewPlaybackController::Command;
    struct Call { C command; qint64 position; play_core::Request request; };
    std::vector<Call> calls;
    play_core::OperationId next=1;
    bool reject=false;
    ReviewPlaybackController controller([&](C command,qint64 position) {
        play_core::Request r{reject ? 0 : next++,7,{}};
        calls.push_back({command,position,r}); return r;
    });
    auto complete=[&](play_core::Request r,play_core::OperationStatus status=play_core::OperationStatus::Succeeded) {
        controller.finished({r.operation,r.session,status,{},{},{}});
    };
    auto finish=[&] { complete(calls.back().request); };
    controller.setDuration(10000);
    require(controller.inPoint()==0 && controller.outPoint()==10000,"default whole range");
    require(!controller.setRange(-1,1000) && !controller.setRange(1000,1000) && !controller.setRange(0,10001),"reject invalid ranges");
    require(controller.setRange(2000,5000),"valid range");
    controller.play(0); require(calls.back().command==C::Pause,"pause before range seek");
    const auto validPending=calls.back().request;
    const auto countBefore=calls.size();
    controller.finished({validPending.operation,999,play_core::OperationStatus::Succeeded,{},{},{}});
    require(calls.size()==countBefore && controller.busy(),"foreign session completion ignored");
    finish(); require(calls.back().command==C::Seek && calls.back().position==2000,"seek range in");
    finish(); require(calls.back().command==C::Play,"play only after seek success"); finish();
    controller.positionChanged(5000,true); require(calls.back().command==C::Pause,"once stops at out");
    const auto count=calls.size(); controller.positionChanged(6000,true); require(calls.size()==count,"boundary coalesced"); finish();
    controller.positionChanged(6000,true); require(calls.size()==count,"stale positions cannot restart once");
    controller.setLoop(true); controller.play(3000); finish();
    controller.positionChanged(5000,true); finish();
    require(calls.back().command==C::Seek && calls.back().position==2000,"loop seeks in");
    const auto stale=calls.back().request;
    controller.pause(); const auto cancelledCount=calls.size(); complete(stale);
    require(calls.size()==cancelledCount,"cancelled seek cannot resume"); finish();
    controller.beginScrub(true); finish(); controller.endScrub(3000,false); finish(); finish();
    require(calls.back().command==C::Play,"scrub restores play intent"); finish();
    controller.beginScrub(true); finish(); controller.endScrub(3000,true); finish();
    const auto before=calls.size(); finish(); require(calls.size()==before,"stop-on-scrub remains paused");
    controller.seek(7000,true); finish(); const auto outsideCount=calls.size(); finish();
    require(calls.size()==outsideCount && !controller.busy(),"manual navigation outside range stays paused");
    controller.seek(4000,true); finish(); complete(calls.back().request,play_core::OperationStatus::Failed);
    require(!controller.busy() && calls.back().command==C::Seek,"failed seek does not resume");
    controller.seek(4000,true); const auto old=calls.back().request; controller.reset(); complete(old);
    require(!controller.busy() && !controller.hasRange(),"reset invalidates old media work");
    controller.setDuration(1000); reject=true; controller.play(0); require(!controller.busy(),"synchronous rejection clears pending");
}
void timelineTests() {
    ReviewTimeline timeline; timeline.resize(800,100);
    timeline.setReviewNotes({{2500,QStringLiteral("问题"),QStringLiteral("test")}},10000);
    require(std::abs(timeline.timeAt(timeline.xAt(2500))-2500)<=1,"timeline mapping round trip");
    const double x=400; const auto anchor=timeline.timeAt(x);
    timeline.zoomAt(4,x); require(std::abs(timeline.timeAt(x)-anchor)<=1,"zoom anchor stable");
    const double span=timeline.visibleSpan(); timeline.panBy(-1000000);
    require(timeline.visibleStart()+span<=10000.01,"pan clamps right boundary");
    timeline.panBy(1000000); require(timeline.visibleStart()==0,"pan clamps left boundary");
    timeline.zoomAt(1e20,400); require(timeline.visibleSpan()>=100,"zoom lower bound");
    timeline.frameAll(); require(timeline.visibleSpan()==10000,"frame whole timeline");
    timeline.setReviewNotes({},9007199254740000LL);
    const qint64 time=8000000000000000LL;
    require(std::abs(timeline.timeAt(timeline.xAt(time))-time)<=2,"long media safe mapping");
    timeline.setReviewNotes({},0); require(timeline.timeAt(500)==0,"empty timeline safe");
    timeline.setReviewNotes({},10000); timeline.show();
    bool started=false,cancelled=false; qint64 committed=-1;
    QObject::connect(&timeline,&ReviewTimeline::scrubStarted,[&]{started=true;});
    QObject::connect(&timeline,&ReviewTimeline::scrubCancelled,[&]{cancelled=true;});
    QObject::connect(&timeline,&ReviewTimeline::scrubFinished,[&](qint64 value){committed=value;});
    QMouseEvent press(QEvent::MouseButtonPress,QPointF(400,30),QPointF(400,30),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&timeline,&press);
    require(started && timeline.isScrubbing(),"mouse begins scrub");
    timeline.cancelScrub(); require(cancelled && committed==-1,"cancel never commits seek");
    QApplication::sendEvent(&timeline,&press);
    QMouseEvent release(QEvent::MouseButtonRelease,QPointF(500,30),QPointF(500,30),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(&timeline,&release);
    require(committed==timeline.timeAt(500) && !timeline.isScrubbing(),"release commits final target once");
    timeline.zoomAt(4,400); timeline.setFollow(false); const auto viewStart=timeline.visibleStart();
    timeline.setPosition(9999); require(timeline.visibleStart()==viewStart,"disabled follow keeps chosen view");
    timeline.grab();
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        controllerTests(); timelineTests();
        QTemporaryDir dir; require(dir.isValid(),"settings temp directory");
        const auto path=dir.filePath("workspace.ini");
        ReviewSettings settings; settings.volume=35; settings.speed=0.5; settings.follow=false; settings.stopOnScrub=true;
        settings.save(path); auto loaded=ReviewSettings::load(path);
        require(loaded.volume==35 && loaded.speed==0.5 && !loaded.follow && loaded.stopOnScrub,"settings round trip");
        { QSettings invalid(path,QSettings::IniFormat); invalid.setValue("reviewWorkspace/v1/volume",500); invalid.setValue("reviewWorkspace/v1/speed","nan"); }
        loaded=ReviewSettings::load(path); require(loaded.volume==100 && loaded.speed==1,"invalid settings defaults");
        QMainWindow window; window.resize(800,600); loaded.restoreWindow(&window);
        std::cout<<"Review timeline, controller and settings passed\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
