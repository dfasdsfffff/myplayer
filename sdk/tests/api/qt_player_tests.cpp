#define SDL_MAIN_HANDLED
#include <SDL.h>
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
#include <algorithm>
#include <map>
#include <limits>
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
        {
            QtPlayer stepping;
            std::map<play_core::OperationId, play_core::OperationResult> operations;
            std::shared_ptr<const play_core::VideoFrame> frame;
            QObject::connect(&stepping, &QtPlayer::operationFinished, &app, [&](auto r) { operations.emplace(r.operation, r); });
            QObject::connect(&stepping, &QtPlayer::videoFrameChanged, &app, [&](auto f) { frame = f; });
            auto complete = [&](play_core::Request r) {
                require(bool(r), "Qt step request rejected");
                require(wait([&] { return operations.contains(r.operation); }), "Qt step operation timed out");
                require(operations.at(r.operation).status == play_core::OperationStatus::Succeeded, "Qt step operation failed");
                return operations.at(r.operation);
            };
            complete(stepping.open(play_core::MediaSource{std::string(u8.begin(), u8.end())}));
            require(complete(stepping.stepFrame(1)).actualPositionMs == 0, "Qt initial frame incorrect");
            require(complete(stepping.stepFrame(1)).actualPositionMs == 100, "Qt next frame incorrect");
            require(complete(stepping.stepFrame(-1)).actualPositionMs == 0, "Qt previous frame incorrect");
            require(frame && frame->timestampMs == 0 && stepping.state() == play_core::PlayerState::Paused,
                "Qt frame and paused state do not match step result");
            complete(stepping.close());
        }
        auto* player = new QtPlayer;
        auto* view = new VideoWidget; view->resize(320,240); view->setPlayer(player); view->show();
        int frames = 0; int results = 0;
        QObject::connect(player,&QtPlayer::videoFrameChanged,&app,[&](auto f) { if (f) ++frames; });
        QObject::connect(player,&QtPlayer::operationFinished,&app,[&](auto) { ++results; });
        require(bool(player->open(play_core::MediaSource{std::string(u8.begin(),u8.end())})),"Open rejected");
        require(bool(player->play()),"Play rejected");
        require(wait([&] { return view->presentedFrames() > 1; }),"Qt widget did not present video");
        const auto snapshot = player->snapshot();
        require(snapshot.session != 0 && snapshot.state == play_core::PlayerState::Playing && snapshot.mediaInfo &&
            snapshot.diagnostics.videoDecodeBackend == play_core::VideoDecodeBackend::Software,
            "Qt snapshot missing video playback diagnostics");
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
        const auto shot = view->grabVideoFrame(); require(!shot.isNull(),"Video widget screenshot is empty");
        const auto screenshotPath = qEnvironmentVariable("PLAYER_SDK_TEST_SCREENSHOT");
        if (!screenshotPath.isEmpty()) require(shot.save(screenshotPath),"Screenshot save failed");
        view->clearExternalSubtitle();
        delete player; require(view->player() == nullptr,"Player destruction left stale view binding");
        delete view;
        require(SDL_InitSubSystem(SDL_INIT_VIDEO)==0,"Host SDL video initialization failed");
        {
        QWidget host; host.resize(400,300); host.show();
        VideoWidget manual(&host); manual.resize(320,240); manual.show();
        auto bgra = std::make_shared<play_core::VideoFrame>();
        bgra->width=64; bgra->height=32; bgra->bytesPerLine=64*4;
        auto pixelsBgra = std::make_shared<std::vector<std::uint8_t>>(64*32*4);
        for (size_t i=0;i<pixelsBgra->size();i+=4) { (*pixelsBgra)[i+1]=255; (*pixelsBgra)[i+3]=255; }
        bgra->bgra = pixelsBgra;
        bgra->sampleAspectRatio={2,1}; bgra->rotationDegrees=90;
        manual.setVideoFrame(bgra);
        require(wait([&] { return manual.presentedFrames()>0; }),"SDL BGRA frame not presented");
        const auto presented=manual.presentedFrames();
        manual.setVideoFrame(bgra); manual.setVideoFrame(bgra);
        require(manual.presentedFrames()>=presented+2,"Nested native SDL surface stalled after first frame");
        auto pixels=manual.grabVideoFrame();
        require(!pixels.isNull() && qGreen(pixels.pixel(pixels.width()/2,pixels.height()/2))>240,"SDL BGRA channel order incorrect");
        require(qGreen(pixels.pixel(10,pixels.height()/2))<10,"SDL SAR/rotation letterbox incorrect");
        manual.resize(400,300); QApplication::processEvents();
        pixels=manual.grabVideoFrame(); require(!pixels.isNull() && pixels.width()>0 && pixels.height()>0 &&
            std::abs(double(pixels.width())/pixels.height()-4./3.)<.02,"SDL resize output aspect stale");
        // Four quadrants make mirror, zoom and pan observable in readback.
        auto quadrants=std::make_shared<play_core::VideoFrame>();
        quadrants->width=64; quadrants->height=32; quadrants->bytesPerLine=256;
        auto data=std::make_shared<std::vector<uint8_t>>(64*32*4,0);
        for (int y=0;y<32;++y) for (int x=0;x<64;++x) {
            const int i=(y*64+x)*4;
            (*data)[i]=(x>=32 && y>=16) ? 255 : 0;
            (*data)[i+1]=x>=32 ? 255 : 0;
            (*data)[i+2]=x<32 ? 255 : 0;
            (*data)[i+3]=255;
        }
        quadrants->bgra=data; manual.setVideoFrame(quadrants);
        require(manual.isViewFit(),"default display remains Fit");
        pixels=manual.grabVideoFrame();
        require(qRed(pixels.pixel(pixels.width()/4,pixels.height()*11/30))>240 && qGreen(pixels.pixel(pixels.width()*3/4,pixels.height()*11/30))>240,"quadrant fixture");
        manual.setMirrors(true,false); pixels=manual.grabVideoFrame();
        require(qGreen(pixels.pixel(pixels.width()/4,pixels.height()*11/30))>240 && qRed(pixels.pixel(pixels.width()*3/4,pixels.height()*11/30))>240,"horizontal mirror pixels");
        manual.setMirrors(false,true); pixels=manual.grabVideoFrame();
        require(qBlue(pixels.pixel(pixels.width()*3/4,pixels.height()*11/30))>240,"vertical mirror pixels");
        quadrants->sampleAspectRatio={2,1}; quadrants->rotationDegrees=90;
        manual.setMirrors(true,false); manual.setVideoFrame(quadrants); pixels=manual.grabVideoFrame();
        require(qGreen(pixels.pixel(pixels.width()/2,pixels.height()/4))>240 && qRed(pixels.pixel(pixels.width()/2,pixels.height()*3/4))>240,"mirror composes with SAR and rotation");
        quadrants->sampleAspectRatio={1,1}; quadrants->rotationDegrees=0; manual.setVideoFrame(quadrants);
        manual.setMirrors(false,false); manual.setViewScale(1); pixels=manual.grabVideoFrame();
        require(!manual.isViewFit() && manual.viewScale()==1 && qRed(pixels.pixel(pixels.width()/2-30,pixels.height()/2-10))>240
            && qRed(pixels.pixel(pixels.width()/2-40,pixels.height()/2-10))<10,"1:1 physical pixel width");
        manual.setViewScale(10); manual.panView(QPointF(100000,100000)); pixels=manual.grabVideoFrame();
        require(std::abs(manual.viewPan().x())<=120.1 && std::abs(manual.viewPan().y())<=10.1,"pan clamped to image bounds");
        const auto oldScale=manual.viewScale(); manual.setViewScale(std::numeric_limits<double>::quiet_NaN());
        require(manual.viewScale()==oldScale,"nonfinite scale rejected");
        manual.fitView(); require(manual.isViewFit() && manual.viewPan().isNull(),"Fit resets pan");
        auto yuv=std::make_shared<play_core::VideoFrame>();
        yuv->format=play_core::VideoFrameFormat::Yuv420P; yuv->width=64; yuv->height=32;
        yuv->colorSpace=play_core::ColorSpace::Bt709; yuv->colorRange=play_core::ColorRange::Limited;
        for(int i=0;i<3;++i) {
            const int stride=i ? 32 : 64, height=i ? 16 : 32;
            auto data=std::shared_ptr<uint8_t>(new uint8_t[stride*height],std::default_delete<uint8_t[]>());
            std::fill_n(data.get(),stride*height,i ? 128 : 235);
            yuv->planes[i]={data,stride,height};
        }
        manual.setVideoFrame(yuv); pixels=manual.grabVideoFrame();
        require(!pixels.isNull() && qRed(pixels.pixel(pixels.width()/2,pixels.height()/2))>240 && qGreen(pixels.pixel(pixels.width()/2,pixels.height()/2))>240,"SDL IYUV limited-range upload incorrect");
        auto fullRangeY = std::shared_ptr<uint8_t>(new uint8_t[64*32],std::default_delete<uint8_t[]>());
        std::fill_n(fullRangeY.get(),64*32,32);
        yuv->planes[0]={fullRangeY,64,32};
        yuv->colorRange=play_core::ColorRange::Full;
        SDL_SetYUVConversionMode(SDL_YUV_CONVERSION_BT601);
        manual.setVideoFrame(yuv); pixels=manual.grabVideoFrame();
        require(!pixels.isNull() && std::abs(qRed(pixels.pixel(pixels.width()/2,pixels.height()/2))-32)<5,
            "SDL IYUV full-range upload used the wrong conversion mode");
        require(SDL_GetYUVConversionMode()==SDL_YUV_CONVERSION_BT601,
            "Video widget did not restore SDL YUV conversion mode");
        yuv->colorRange=play_core::ColorRange::Limited;
        std::fill_n(fullRangeY.get(),64*32,235);
        manual.setVideoFrame(yuv);
        auto subtitle=std::make_shared<play_core::SubtitleFrame>();
        subtitle->startMs=0; subtitle->endMs=1000;
        play_core::SubtitleBitmap bitmap;
        bitmap.x=30; bitmap.y=14; bitmap.width=8; bitmap.height=8; bitmap.stride=32;
        bitmap.bgra.resize(8*8*4);
        for(size_t i=0;i<bitmap.bgra.size();i+=4) { bitmap.bgra[i+2]=255; bitmap.bgra[i+3]=255; }
        subtitle->bitmaps.push_back(std::move(bitmap));
        manual.setSubtitleFrame(subtitle);
        pixels=manual.grabVideoFrame();
        require(!pixels.isNull() && qRed(pixels.pixel(pixels.width()/2,pixels.height()*8/15))>200 && qGreen(pixels.pixel(pixels.width()/2,pixels.height()*8/15))<100,
            "SDL bitmap subtitle not drawn");
        pixels=manual.grabVideoFrame();
        require(qRed(pixels.pixel(pixels.width()/2,pixels.height()*8/15))>200 && qGreen(pixels.pixel(pixels.width()/2,pixels.height()*8/15))<100,
            "Cached subtitle lost on redraw");
        manual.setSubtitleFrame({});
        pixels=manual.grabVideoFrame();
        require(qGreen(pixels.pixel(pixels.width()/2,pixels.height()*8/15))>200,"Cleared subtitle texture still drawn");
        yuv->colorSpace=play_core::ColorSpace::Bt2020; manual.setVideoFrame(yuv);
        pixels=manual.grabVideoFrame(); require(!pixels.isNull() && qGreen(pixels.pixel(pixels.width()/2,pixels.height()/2))>240,"SDL BT2020 fallback failed");
        manual.setVideoFrame({}); require(manual.grabVideoFrame().isNull(),"SDL clear retained stale output");
        }
        require((SDL_WasInit(SDL_INIT_VIDEO)&SDL_INIT_VIDEO)!=0,"SDK teardown destroyed host SDL video state");
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        QtPlayer other; auto* first = new VideoWidget; auto* second = new VideoWidget;
        first->setPlayer(&other); second->setPlayer(&other);
        require(first->player() == nullptr && second->player() == &other,"Single view binding not enforced");
        delete first; delete second;
        other.close(); QApplication::processEvents();
        std::cout << "Qt video, bounded delivery, subtitles and destruction order passed\n"; return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
