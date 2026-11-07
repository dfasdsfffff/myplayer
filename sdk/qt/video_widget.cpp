#include <player_sdk_qt/video_widget.h>
#include <player_sdk_qt/qt_player.h>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "sdl_video_surface.h"
#include <QPainter>
#include <QGuiApplication>
#include <QResizeEvent>
#include <QDebug>
#include <vector>
#include <QFile>
#include <QRegularExpression>
#include <QVariant>
#include <cmath>
extern "C" {
#include <libswscale/swscale.h>
#include <ass/ass.h>
}
namespace player_sdk_qt {
struct VideoWidget::Impl {
    QPointer<QtPlayer> player;
    SdlVideoSurface* surface{nullptr};
    SDL_Window* window{nullptr};
    SDL_Renderer* videoRenderer{nullptr};
    SDL_Texture* texture{nullptr};
    SDL_Surface* softwareSurface{nullptr};
    QSize textureSize;
    Uint32 textureFormat{0};
    bool videoReference{false};
    bool needsUpload{false};
    bool headless{QGuiApplication::platformName() == "offscreen" || QGuiApplication::platformName() == "minimal"};
    QImage readback;
    std::vector<uint8_t> converted;
    std::shared_ptr<const play_core::VideoFrame> frame;
    std::shared_ptr<const play_core::SubtitleFrame> subtitle;
    qint64 position{0};
    quint64 presented{0};
    bool fresh{false};
    ASS_Library* library{ass_library_init()};
    ASS_Renderer* renderer{library ? ass_renderer_init(library) : nullptr};
    ASS_Track* embedded{nullptr};
    ASS_Track* external{nullptr};
    SwsContext* scaler{nullptr};
    std::vector<SDL_Texture*> bitmapTextures;
    std::vector<SDL_Texture*> assTextures;
    QSize assSize;
    void clearTextures(std::vector<SDL_Texture*>& textures) {
        for (auto* item : textures) SDL_DestroyTexture(item);
        textures.clear();
    }
    Impl() { if (renderer) ass_set_fonts(renderer, nullptr, "Arial", 1, nullptr, 1); }
    ~Impl() {
        if (surface) { surface->repaint = {}; surface->releasing = {}; }
        destroyVideo();
        if (videoReference) SDL_QuitSubSystem(SDL_INIT_VIDEO);
        if (embedded) ass_free_track(embedded);
        if (external) ass_free_track(external);
        if (renderer) ass_renderer_done(renderer);
        if (library) ass_library_done(library);
        sws_freeContext(scaler);
    }
    void setSubtitle(std::shared_ptr<const play_core::SubtitleFrame> s) {
        subtitle = std::move(s);
        clearTextures(bitmapTextures);
        clearTextures(assTextures);
        assSize = {};
        if (embedded) { ass_free_track(embedded); embedded = nullptr; }
        if (subtitle && !subtitle->assOrText.empty() && library) {
            embedded = ass_new_track(library);
            if (embedded) {
                if (subtitle->assPacket && !subtitle->assHeader.empty()) {
                    auto header = subtitle->assHeader; auto packet = subtitle->assOrText;
                    ass_process_codec_private(embedded,header.data(),static_cast<int>(header.size()));
                    ass_process_chunk(embedded,packet.data(),static_cast<int>(packet.size()),
                        subtitle->startMs,std::max<qint64>(1,subtitle->endMs-subtitle->startMs));
                } else if (subtitle->assOrText.find("[Script Info]") != std::string::npos)
                    ass_process_data(embedded,subtitle->assOrText.data(),static_cast<int>(subtitle->assOrText.size()));
                else {
                    std::string text = subtitle->assOrText;
                    std::string escaped;
                    for (char ch : text) {
                        if (ch == '\n') escaped += "\\N";
                        else if (ch == '{' || ch == '}' || ch == '\\') { escaped += '\\'; escaped += ch; }
                        else escaped += ch;
                    }
                    std::string header = "[Script Info]\nScriptType: v4.00+\nPlayResX: 384\nPlayResY: 288\n[V4+ Styles]\n"
                        "Format: Name,Fontname,Fontsize,PrimaryColour,SecondaryColour,OutlineColour,BackColour,Bold,Italic,Underline,StrikeOut,ScaleX,ScaleY,Spacing,Angle,BorderStyle,Outline,Shadow,Alignment,MarginL,MarginR,MarginV,Encoding\n"
                        "Style: Default,Arial,24,&H00FFFFFF,&H000000FF,&H00000000,&H64000000,0,0,0,0,100,100,0,0,1,1,0,2,10,10,10,1\n[Events]\n"
                        "Format: Layer,Start,End,Style,Name,MarginL,MarginR,MarginV,Effect,Text\n";
                    ass_process_codec_private(embedded,header.data(),static_cast<int>(header.size()));
                    auto packet = "0,0,Default,,0,0,0,," + escaped;
                    ass_process_chunk(embedded,packet.data(),static_cast<int>(packet.size()),subtitle->startMs,
                        std::max<qint64>(1,subtitle->endMs-subtitle->startMs));
                }
            }
        }
    }
    void setFrame(std::shared_ptr<const play_core::VideoFrame> f) {
        frame = std::move(f); fresh = bool(frame); needsUpload = bool(frame); readback = {};
        if (!frame) destroyVideo();
        if (surface) surface->setVisible(frame && !headless);
    }
    void destroyVideo() {
        if (texture) SDL_DestroyTexture(texture);
        clearTextures(bitmapTextures);
        clearTextures(assTextures);
        if (videoRenderer) SDL_DestroyRenderer(videoRenderer);
        if (window) SDL_DestroyWindow(window); // SDL_CreateWindowFrom does not own Qt's native window.
        if (softwareSurface) SDL_FreeSurface(softwareSurface);
        texture = nullptr; videoRenderer = nullptr; window = nullptr; softwareSurface = nullptr;
        textureSize = {}; textureFormat = 0; needsUpload = bool(frame);
    }
    bool ensureVideo(QSize size) {
        if (videoRenderer && (!headless || QSize(softwareSurface->w,softwareSurface->h) == size)) return true;
        destroyVideo();
        if (!videoReference) {
            // Always take our own reference, even if the host initialized SDL first.
            if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) return false;
            videoReference = true;
        }
        if (headless) {
            softwareSurface = SDL_CreateRGBSurfaceWithFormat(0,size.width(),size.height(),32,SDL_PIXELFORMAT_ARGB8888);
            if (softwareSurface) videoRenderer = SDL_CreateSoftwareRenderer(softwareSurface);
        } else {
            window = SDL_CreateWindowFrom(reinterpret_cast<void*>(surface->winId()));
            if (window) {
                videoRenderer = SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED);
                if (!videoRenderer) videoRenderer = SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
            }
        }
        if (!videoRenderer) { qWarning() << "SDL video renderer:" << SDL_GetError(); destroyVideo(); return false; }
        return true;
    }
    bool upload() {
        const auto& f = *frame;
        const bool yuv = f.format == play_core::VideoFrameFormat::Yuv420P;
        if (f.width <= 0 || f.height <= 0) return false;
        if (yuv) {
            for (int i=0;i<3;++i) {
                const int width = i ? (f.width+1)/2 : f.width;
                const int height = i ? (f.height+1)/2 : f.height;
                if (!f.planes[i].data || f.planes[i].stride < width || f.planes[i].height < height) return false;
            }
        } else if (!f.bgra || f.bytesPerLine < static_cast<int64_t>(f.width)*4 ||
            f.bgra->size() < static_cast<size_t>(f.bytesPerLine)*f.height) return false;
        // SDL2 has no BT.2020 conversion mode. Keep a targeted CPU fallback for
        // that color space; ordinary YUV420P is uploaded directly as IYUV.
        const bool convert = yuv && f.colorSpace == play_core::ColorSpace::Bt2020;
        const Uint32 format = yuv && !convert ? SDL_PIXELFORMAT_IYUV : SDL_PIXELFORMAT_BGRA32;
        if (!texture || textureSize != QSize(f.width,f.height) || textureFormat != format) {
            if (texture) SDL_DestroyTexture(texture);
            texture = SDL_CreateTexture(videoRenderer,format,SDL_TEXTUREACCESS_STREAMING,f.width,f.height);
            textureSize = QSize(f.width,f.height); textureFormat = format;
            if (!texture) return false;
            SDL_SetTextureScaleMode(texture,SDL_ScaleModeLinear);
            needsUpload = true;
        }
        if (!needsUpload) return true;
        int result;
        if (format == SDL_PIXELFORMAT_IYUV) {
            result = SDL_UpdateYUVTexture(texture,nullptr,f.planes[0].data.get(),f.planes[0].stride,
                f.planes[1].data.get(),f.planes[1].stride,f.planes[2].data.get(),f.planes[2].stride);
        } else if (convert) {
            scaler = sws_getCachedContext(scaler,f.width,f.height,AV_PIX_FMT_YUV420P,
                f.width,f.height,AV_PIX_FMT_BGRA,SWS_BILINEAR,nullptr,nullptr,nullptr);
            if (!scaler) return false;
            sws_setColorspaceDetails(scaler,sws_getCoefficients(SWS_CS_BT2020),f.colorRange == play_core::ColorRange::Full,
                sws_getCoefficients(SWS_CS_BT2020),1,0,1<<16,1<<16);
            converted.resize(static_cast<size_t>(f.width)*f.height*4);
            const uint8_t* src[4]{f.planes[0].data.get(),f.planes[1].data.get(),f.planes[2].data.get(),nullptr};
            int strides[4]{f.planes[0].stride,f.planes[1].stride,f.planes[2].stride,0};
            uint8_t* dst[4]{converted.data(),nullptr,nullptr,nullptr}; int ds[4]{f.width*4,0,0,0};
            if (sws_scale(scaler,src,strides,0,f.height,dst,ds) != f.height) return false;
            result = SDL_UpdateTexture(texture,nullptr,converted.data(),f.width*4);
        } else result = SDL_UpdateTexture(texture,nullptr,f.bgra->data(),f.bytesPerLine);
        if (result == 0) needsUpload = false;
        return result == 0;
    }
    SDL_Texture* cachedBitmap(std::vector<SDL_Texture*>& cache,size_t index,
        const uint8_t* data,int width,int height,int stride) {
        if (!data || width<=0 || height<=0 || stride < static_cast<int64_t>(width)*4) return nullptr;
        if (cache.size() <= index) cache.resize(index+1,nullptr);
        if (cache[index]) return cache[index];
        auto* bitmap = SDL_CreateTexture(videoRenderer,SDL_PIXELFORMAT_BGRA32,SDL_TEXTUREACCESS_STATIC,width,height);
        if (!bitmap) return nullptr;
        SDL_SetTextureBlendMode(bitmap,SDL_BLENDMODE_BLEND);
        if (SDL_UpdateTexture(bitmap,nullptr,data,stride) != 0) { SDL_DestroyTexture(bitmap); return nullptr; }
        cache[index] = bitmap;
        return bitmap;
    }
    bool render(QSize size,bool capture = false) {
        if (!frame || size.isEmpty() || !ensureVideo(size) || !upload()) return false;
        int width=size.width(),height=size.height(); SDL_GetRendererOutputSize(videoRenderer,&width,&height);
        const auto& f = *frame;
        const double sar = f.sampleAspectRatio.num>0 && f.sampleAspectRatio.den>0 ? double(f.sampleAspectRatio.num)/f.sampleAspectRatio.den : 1.;
        const double angle = std::isfinite(f.rotationDegrees) ? f.rotationDegrees : 0;
        const double radians = angle * 3.14159265358979323846 / 180.;
        const double cw = f.width*sar, ch = f.height;
        const double boundW = std::abs(cw*std::cos(radians))+std::abs(ch*std::sin(radians));
        const double boundH = std::abs(cw*std::sin(radians))+std::abs(ch*std::cos(radians));
        const double scale = std::min(width/boundW,height/boundH);
        SDL_Rect target{static_cast<int>(std::lround((width-cw*scale)/2)),static_cast<int>(std::lround((height-ch*scale)/2)),
            std::max(1,static_cast<int>(std::lround(cw*scale))),std::max(1,static_cast<int>(std::lround(ch*scale)))};
        SDL_SetRenderDrawColor(videoRenderer,0,0,0,255);
        if (SDL_RenderClear(videoRenderer) != 0) return false;
        // SDL2 conversion mode is global: set/restore around each synchronous
        // draw on the QObject GUI thread so mixed-color multi-view playback works.
        const auto oldMode = SDL_GetYUVConversionMode();
        SDL_SetYUVConversionMode(f.colorRange == play_core::ColorRange::Full ? SDL_YUV_CONVERSION_JPEG :
            f.colorSpace == play_core::ColorSpace::Bt709 ? SDL_YUV_CONVERSION_BT709 : SDL_YUV_CONVERSION_BT601);
        const int copied = SDL_RenderCopyEx(videoRenderer,texture,nullptr,&target,angle,nullptr,SDL_FLIP_NONE);
        SDL_RenderFlush(videoRenderer);
        SDL_SetYUVConversionMode(oldMode);
        if (copied != 0) return false;
        if (!external && subtitle && position >= subtitle->startMs && position <= subtitle->endMs) {
            for (size_t i=0;i<subtitle->bitmaps.size();++i) {
                const auto& b = subtitle->bitmaps[i];
                if (b.width<=0 || b.height<=0 || b.stride<=0 || b.bgra.size()<static_cast<size_t>(b.stride)*b.height) continue;
                const double dx=(b.x+b.width/2.)*sar-cw/2., dy=b.y+b.height/2.-ch/2.;
                const int bw=static_cast<int>(std::lround(b.width*sar*scale)), bh=static_cast<int>(std::lround(b.height*scale));
                SDL_Rect dest{static_cast<int>(std::lround(width/2.+scale*(dx*std::cos(radians)-dy*std::sin(radians))-bw/2.)),
                    static_cast<int>(std::lround(height/2.+scale*(dx*std::sin(radians)+dy*std::cos(radians))-bh/2.)),bw,bh};
                if (auto* bitmap=cachedBitmap(bitmapTextures,i,b.bgra.data(),b.width,b.height,b.stride))
                    SDL_RenderCopyEx(videoRenderer,bitmap,nullptr,&dest,angle,nullptr,SDL_FLIP_NONE);
            }
        }
        auto* track = external ? external : embedded;
        if (track && renderer) {
            if (assSize != QSize(width,height)) { clearTextures(assTextures); assSize = QSize(width,height); }
            ass_set_frame_size(renderer,width,height); int changed=0;
            auto* images=ass_render_frame(renderer,track,position,&changed);
            if (changed == 2) clearTextures(assTextures);
            size_t index=0;
            for (auto* img=images;img;img=img->next) {
                if (img->w<=0 || img->h<=0) continue;
                if (assTextures.size()<=index || !assTextures[index]) {
                    std::vector<uint8_t> pixels(static_cast<size_t>(img->w)*img->h*4);
                    for (int y=0;y<img->h;++y) for(int x=0;x<img->w;++x) {
                        auto* pixel=&pixels[(static_cast<size_t>(y)*img->w+x)*4];
                        pixel[0]=(img->color>>8)&255; pixel[1]=(img->color>>16)&255; pixel[2]=(img->color>>24)&255;
                        pixel[3]=static_cast<uint8_t>(img->bitmap[y*img->stride+x]*(255-(img->color&255))/255);
                    }
                    cachedBitmap(assTextures,index,pixels.data(),img->w,img->h,img->w*4);
                }
                if (assTextures.size()>index && assTextures[index]) {
                    SDL_Rect dest{img->dst_x,img->dst_y,img->w,img->h};
                    SDL_RenderCopyEx(videoRenderer,assTextures[index],nullptr,&dest,0,nullptr,SDL_FLIP_NONE);
                }
                ++index;
            }
        }
        if (capture) {
            readback=QImage(width,height,QImage::Format_ARGB32);
            if (SDL_RenderReadPixels(videoRenderer,nullptr,SDL_PIXELFORMAT_ARGB8888,readback.bits(),static_cast<int>(readback.bytesPerLine())) != 0)
                readback={};
        }
        SDL_RenderPresent(videoRenderer);
        return true;
    }
};
VideoWidget::VideoWidget(QWidget* parent) : QWidget(parent), impl_(std::make_unique<Impl>()) {
    setAttribute(Qt::WA_OpaquePaintEvent); setMinimumSize(16,16);
    impl_->surface = new SdlVideoSurface(this);
    impl_->surface->hide();
    impl_->surface->repaint = [this] { present(); };
    impl_->surface->setGeometry(rect());
    impl_->surface->releasing = [this] { impl_->destroyVideo(); };
}
VideoWidget::~VideoWidget() = default;
QtPlayer* VideoWidget::player() const { return impl_->player; }
void VideoWidget::setVideoFrame(std::shared_ptr<const play_core::VideoFrame> frame) { impl_->setFrame(std::move(frame)); update(); present(); }
void VideoWidget::setSubtitleFrame(std::shared_ptr<const play_core::SubtitleFrame> frame) { impl_->setSubtitle(std::move(frame)); update(); present(); }
void VideoWidget::setPosition(qint64 milliseconds) { impl_->position = milliseconds; update(); present(); }
bool VideoWidget::hasExternalSubtitle() const { return impl_->external != nullptr; }
quint64 VideoWidget::presentedFrames() const { return impl_->presented; }
void VideoWidget::setPlayer(QtPlayer* p) {
    if (impl_->player == p) return;
    if (p) {
        // Enforce the single-view contract instead of silently sharing views.
        if (auto* bound = p->property("_player_sdk_view").value<QObject*>(); bound && bound != this)
            static_cast<VideoWidget*>(bound)->setPlayer(nullptr);
    }
    if (impl_->player) { impl_->player->setProperty("_player_sdk_view", QVariant()); disconnect(impl_->player, nullptr, this, nullptr); }
    impl_->player = p; impl_->setFrame({}); impl_->setSubtitle({}); impl_->position = 0; update();
    if (!p) return;
    p->setProperty("_player_sdk_view", QVariant::fromValue(static_cast<QObject*>(this)));
    connect(this, &QObject::destroyed, p, [p, self = static_cast<QObject*>(this)] {
        if (p->property("_player_sdk_view").value<QObject*>() == self) p->setProperty("_player_sdk_view", QVariant());
    });
    connect(p,&QtPlayer::videoFrameChanged,this,&VideoWidget::setVideoFrame);
    connect(p,&QtPlayer::subtitleChanged,this,&VideoWidget::setSubtitleFrame);
    connect(p,&QtPlayer::positionChanged,this,&VideoWidget::setPosition);
    connect(p, &QObject::destroyed, this, [this] { impl_->player = nullptr; impl_->setFrame({}); impl_->setSubtitle({}); update(); });
}
bool VideoWidget::loadExternalSubtitle(const QString& filename) {
    if (!impl_->library) return false;
    QFile file(filename); if (!file.open(QIODevice::ReadOnly)) return false;
    auto data = file.readAll();
    if (filename.endsWith(".srt", Qt::CaseInsensitive)) {
        QString ass = "[Script Info]\nScriptType: v4.00+\nPlayResX: 384\nPlayResY: 288\n[V4+ Styles]\n"
            "Format: Name,Fontname,Fontsize,PrimaryColour,SecondaryColour,OutlineColour,BackColour,Bold,Italic,Underline,StrikeOut,ScaleX,ScaleY,Spacing,Angle,BorderStyle,Outline,Shadow,Alignment,MarginL,MarginR,MarginV,Encoding\n"
            "Style: Default,Arial,24,&H00FFFFFF,&H000000FF,&H00000000,&H64000000,0,0,0,0,100,100,0,0,1,1,0,2,10,10,10,1\n[Events]\n"
            "Format: Layer,Start,End,Style,Name,MarginL,MarginR,MarginV,Effect,Text\n";
        const auto blocks = QString::fromUtf8(data).replace("\r", "").split("\n\n", Qt::SkipEmptyParts);
        const QRegularExpression timing("(\\d+):(\\d+):(\\d+),(\\d+)\\s*-->\\s*(\\d+):(\\d+):(\\d+),(\\d+)");
        for (const auto& block : blocks) {
            const auto match = timing.match(block); if (!match.hasMatch()) continue;
            auto ts = [&](int i) { return QString("%1:%2:%3.%4").arg(match.captured(i).toInt()).arg(match.captured(i+1)).arg(match.captured(i+2)).arg(match.captured(i+3).left(2)); };
            auto text = block.mid(match.capturedEnd()).trimmed().replace("\n", "\\N");
            ass += QString("Dialogue: 0,%1,%2,Default,,0,0,0,,%3\n").arg(ts(1),ts(5),text);
        }
        data = ass.toUtf8();
    }
    auto* track = ass_read_memory(impl_->library, data.data(), data.size(), nullptr);
    if (!track) return false;
    if (impl_->external) ass_free_track(impl_->external);
    impl_->external = track; impl_->clearTextures(impl_->assTextures); update(); present(); return true;
}
void VideoWidget::clearExternalSubtitle() {
    if (impl_->external) { ass_free_track(impl_->external); impl_->external = nullptr; }
    impl_->clearTextures(impl_->assTextures); update(); present();
}
void VideoWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.fillRect(rect(),Qt::black); // Empty/background only; video is SDL.
    painter.end();
    present();
}
void VideoWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    impl_->surface->setGeometry(rect()); present();
}
void VideoWidget::present() {
    if (!isVisible()) return;
    if (impl_->render(size()) && impl_->fresh) {
        impl_->fresh=false; ++impl_->presented; emit framePresented();
    }
}
QImage VideoWidget::grabVideoFrame() {
    if (!impl_->render(size(),true)) return {};
    return impl_->readback;
}
}
