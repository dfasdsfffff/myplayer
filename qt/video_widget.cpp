#include <player_sdk_qt/video_widget.h>
#include <player_sdk_qt/qt_player.h>
#include <QPainter>
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
    QImage image;
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
    Impl() { if (renderer) ass_set_fonts(renderer, nullptr, "Arial", 1, nullptr, 1); }
    ~Impl() {
        if (embedded) ass_free_track(embedded);
        if (external) ass_free_track(external);
        if (renderer) ass_renderer_done(renderer);
        if (library) ass_library_done(library);
        sws_freeContext(scaler);
    }
    void setSubtitle(std::shared_ptr<const play_core::SubtitleFrame> s) {
        subtitle = std::move(s);
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
            }
        }
    }
    void setFrame(std::shared_ptr<const play_core::VideoFrame> f) {
        frame = std::move(f); image = {}; fresh = false;
        if (!frame) return;
        const auto& n = *frame;
        if (n.width <= 0 || n.height <= 0) return;
        if (n.format == play_core::VideoFrameFormat::Bgra32) {
            image = QImage(n.bgra.data(), n.width, n.height, n.bytesPerLine, QImage::Format_ARGB32).copy();
        } else {
            scaler = sws_getCachedContext(scaler, n.width, n.height, AV_PIX_FMT_YUV420P,
                n.width, n.height, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!scaler) return;
            const int colors = n.colorSpace == play_core::ColorSpace::Bt709 ? SWS_CS_ITU709 :
                n.colorSpace == play_core::ColorSpace::Bt2020 ? SWS_CS_BT2020 : SWS_CS_ITU601;
            sws_setColorspaceDetails(scaler, sws_getCoefficients(colors), n.colorRange == play_core::ColorRange::Full,
                sws_getCoefficients(colors), 1, 0, 1 << 16, 1 << 16);
            image = QImage(n.width, n.height, QImage::Format_ARGB32);
            const uint8_t* src[4]{n.planes[0].data.get(), n.planes[1].data.get(), n.planes[2].data.get(), nullptr};
            int strides[4]{n.planes[0].stride,n.planes[1].stride,n.planes[2].stride,0};
            uint8_t* dst[4]{image.bits(),nullptr,nullptr,nullptr}; int ds[4]{static_cast<int>(image.bytesPerLine()),0,0,0};
            sws_scale(scaler, src, strides, 0, n.height, dst, ds);
        }
        fresh = !image.isNull();
    }
};
VideoWidget::VideoWidget(QWidget* parent) : QWidget(parent), impl_(std::make_unique<Impl>()) {
    setAttribute(Qt::WA_OpaquePaintEvent); setMinimumSize(16,16);
}
VideoWidget::~VideoWidget() = default;
QtPlayer* VideoWidget::player() const { return impl_->player; }
void VideoWidget::setVideoFrame(std::shared_ptr<const play_core::VideoFrame> frame) { impl_->setFrame(std::move(frame)); update(); }
void VideoWidget::setSubtitleFrame(std::shared_ptr<const play_core::SubtitleFrame> frame) { impl_->setSubtitle(std::move(frame)); update(); }
void VideoWidget::setPosition(qint64 milliseconds) { impl_->position = milliseconds; update(); }
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
    connect(p, &QtPlayer::videoFrameChanged, this, [this](auto frame) { impl_->setFrame(std::move(frame)); update(); });
    connect(p, &QtPlayer::subtitleChanged, this, [this](auto sub) { impl_->setSubtitle(std::move(sub)); update(); });
    connect(p, &QtPlayer::positionChanged, this, [this](qint64 ms) { impl_->position = ms; update(); });
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
    clearExternalSubtitle(); impl_->external = track; update(); return true;
}
void VideoWidget::clearExternalSubtitle() {
    if (impl_->external) { ass_free_track(impl_->external); impl_->external = nullptr; } update();
}
void VideoWidget::paintEvent(QPaintEvent*) {
    QPainter p(this); p.fillRect(rect(), Qt::black);
    if (impl_->image.isNull() || !impl_->frame) return;
    const auto& f = *impl_->frame;
    double sar = f.sampleAspectRatio.den > 0 && f.sampleAspectRatio.num > 0 ? double(f.sampleAspectRatio.num) / f.sampleAspectRatio.den : 1;
    QSizeF unrotated(f.width * sar, f.height);
    const bool quarter = std::abs(std::fmod(std::abs(f.rotationDegrees),180.) - 90.) < .01;
    QSizeF bounds = quarter ? QSizeF(unrotated.height(),unrotated.width()) : unrotated;
    const double scale = std::min(width()/bounds.width(), height()/bounds.height());
    p.save(); p.translate(rect().center()); p.rotate(f.rotationDegrees); p.scale(scale,scale);
    const QRectF target(-unrotated.width()/2,-unrotated.height()/2,unrotated.width(),unrotated.height());
    p.drawImage(target,impl_->image);
    if (impl_->subtitle && impl_->position >= impl_->subtitle->startMs && impl_->position <= impl_->subtitle->endMs) {
        for (const auto& b : impl_->subtitle->bitmaps) {
            QImage bitmap(b.bgra.data(), b.width,b.height,b.stride,QImage::Format_ARGB32);
            p.drawImage(QRectF(target.left()+b.x*sar,target.top()+b.y,b.width*sar,b.height),bitmap);
        }
    }
    p.restore();
    if (!impl_->external && impl_->subtitle && !impl_->subtitle->assPacket &&
        impl_->subtitle->assOrText.find("[Script Info]") == std::string::npos &&
        impl_->position >= impl_->subtitle->startMs && impl_->position <= impl_->subtitle->endMs) {
        const QString text = QString::fromUtf8(impl_->subtitle->assOrText);
        const auto bounds = rect().adjusted(12,12,-12,-12);
        auto font = p.font(); font.setPixelSize(std::max(14,height()/22)); p.setFont(font);
        p.setPen(Qt::black); p.drawText(bounds.translated(1,1),Qt::AlignHCenter|Qt::AlignBottom|Qt::TextWordWrap,text);
        p.setPen(Qt::white); p.drawText(bounds,Qt::AlignHCenter|Qt::AlignBottom|Qt::TextWordWrap,text);
    }
    auto* track = impl_->external ? impl_->external : impl_->embedded;
    if (track && impl_->renderer) {
        ass_set_frame_size(impl_->renderer,width(),height()); int changed = 0;
        auto* img = ass_render_frame(impl_->renderer,track,impl_->position,&changed);
        for (; img; img = img->next) {
            QImage glyph(img->w,img->h,QImage::Format_ARGB32);
            const auto color = img->color;
            for (int y=0;y<img->h;++y) for(int x=0;x<img->w;++x) {
                const int alpha = img->bitmap[y*img->stride+x] * (255-(color & 255)) / 255;
                glyph.setPixel(x,y,qRgba((color>>24)&255,(color>>16)&255,(color>>8)&255,alpha));
            }
            p.drawImage(img->dst_x,img->dst_y,glyph);
        }
    }
    if (impl_->fresh) { ++impl_->presented; impl_->fresh = false; emit framePresented(); }
}
}
