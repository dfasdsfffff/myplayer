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
        require(!pixels.isNull() && qRed(pixels.pixel(200,150))>240 && qGreen(pixels.pixel(200,150))>240,"SDL IYUV limited-range upload incorrect");
        auto fullRangeY = std::shared_ptr<uint8_t>(new uint8_t[64*32],std::default_delete<uint8_t[]>());
        std::fill_n(fullRangeY.get(),64*32,32);
        yuv->planes[0]={fullRangeY,64,32};
        yuv->colorRange=play_core::ColorRange::Full;
        SDL_SetYUVConversionMode(SDL_YUV_CONVERSION_BT601);
        manual.setVideoFrame(yuv); pixels=manual.grabVideoFrame();
        require(!pixels.isNull() && std::abs(qRed(pixels.pixel(200,150))-32)<5,
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
        require(!pixels.isNull() && qRed(pixels.pixel(200,160))>200 && qGreen(pixels.pixel(200,160))<100,
            "SDL bitmap subtitle not drawn");
        pixels=manual.grabVideoFrame();
        require(qRed(pixels.pixel(200,160))>200 && qGreen(pixels.pixel(200,160))<100,
            "Cached subtitle lost on redraw");
        manual.setSubtitleFrame({});
        pixels=manual.grabVideoFrame();
        require(qGreen(pixels.pixel(200,160))>200,"Cleared subtitle texture still drawn");
        yuv->colorSpace=play_core::ColorSpace::Bt2020; manual.setVideoFrame(yuv);
        pixels=manual.grabVideoFrame(); require(!pixels.isNull() && qGreen(pixels.pixel(200,150))>240,"SDL BT2020 fallback failed");
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
