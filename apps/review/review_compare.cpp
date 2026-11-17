#include "review_compare.h"

#include <player_sdk_qt/qt_player.h>
#include <player_sdk_qt/video_widget.h>

#include <QFileInfo>
#include <QLabel>
#include <QPainter>
#include <QQueue>
#include <QResizeEvent>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <optional>
#include <utility>

namespace review {
namespace {

constexpr qint64 UnknownDuration = 0;
constexpr int CompositeHz = 12;
constexpr qint64 MaxCompositePixels = 1920LL * 1080LL;

QString requestError(const play_core::Request& request)
{
    return request.error.message.empty()
        ? QObject::tr("B 播放器拒绝了此操作。")
        : QString::fromStdString(request.error.message);
}

QString resultError(const play_core::OperationResult& result)
{
    return result.error.message.empty()
        ? QObject::tr("B 播放操作失败。")
        : QString::fromStdString(result.error.message);
}

class CompositeCanvas final : public QWidget {
public:
    explicit CompositeCanvas(QWidget* parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAutoFillBackground(false);
    }

    void setFrames(const QImage& a, const QImage& b, ReviewCompare::Mode mode,
                   double wipe, double overlay)
    {
        a_ = a;
        b_ = b;
        mode_ = mode;
        wipe_ = wipe;
        overlay_ = overlay;
        output_ = compose();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        if (!output_.isNull())
            painter.drawImage(rect(), output_);
    }

private:
    QImage scaled(const QImage& image, const QSize& size) const
    {
        if (!image.isNull())
            return image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                .convertToFormat(QImage::Format_ARGB32);
        QImage black(size, QImage::Format_ARGB32);
        black.fill(Qt::black);
        return black;
    }

    QImage compose() const
    {
        QSize size = this->size();
        if (size.isEmpty())
            return {};
        const qint64 pixels = qint64(size.width()) * size.height();
        if (pixels > MaxCompositePixels) {
            const double scale = std::sqrt(double(MaxCompositePixels) / double(pixels));
            size = QSize(std::max(1, qFloor(size.width() * scale)),
                         std::max(1, qFloor(size.height() * scale)));
        }
        QImage a = scaled(a_, size);
        QImage b = scaled(b_, size);
        QImage out(size, QImage::Format_ARGB32);
        out.fill(Qt::black);
        QPainter painter(&out);
        switch (mode_) {
        case ReviewCompare::Mode::Wipe:
            painter.drawImage(QPoint(), a);
            painter.setClipRect(QRect(0, 0, qRound(size.width() * wipe_), size.height()));
            painter.drawImage(QPoint(), b);
            break;
        case ReviewCompare::Mode::Overlay:
            painter.drawImage(QPoint(), a);
            painter.setOpacity(overlay_);
            painter.drawImage(QPoint(), b);
            break;
        case ReviewCompare::Mode::Difference: {
            painter.end();
            auto* dst = reinterpret_cast<QRgb*>(out.bits());
            for (int y = 0; y < size.height(); ++y) {
                const auto* pa = reinterpret_cast<const QRgb*>(a.constScanLine(y));
                const auto* pb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
                for (int x = 0; x < size.width(); ++x) {
                    const int r = std::abs(qRed(pa[x]) - qRed(pb[x]));
                    const int g = std::abs(qGreen(pa[x]) - qGreen(pb[x]));
                    const int bl = std::abs(qBlue(pa[x]) - qBlue(pb[x]));
                    dst[y * out.bytesPerLine() / 4 + x] = qRgba(r, g, bl, 255);
                }
            }
            return out;
        }
        case ReviewCompare::Mode::SideBySide:
            painter.drawImage(QRect(0, 0, size.width() / 2, size.height()), a);
            painter.drawImage(QRect(size.width() / 2, 0, size.width() - size.width() / 2,
                                    size.height()), b);
            break;
        }
        painter.end();
        return out;
    }

    QImage a_;
    QImage b_;
    QImage output_;
    ReviewCompare::Mode mode_{ReviewCompare::Mode::Wipe};
    double wipe_{0.5};
    double overlay_{0.75};
};

} // namespace

struct ReviewCompare::Impl {
    enum class CommandType { Open, Pause, Seek, Play, Rate, Mute };
    struct Command {
        CommandType type;
        qint64 value = 0;
        double rate = 1.0;
        QString path;
    };
    struct InFlight {
        Command command;
        play_core::Request request;
        quint64 generation = 0;
    };

    ReviewCompare* q = nullptr;
    QPointer<player_sdk_qt::QtPlayer> primary;
    player_sdk_qt::QtPlayer* secondary = nullptr;
    quint64 primaryGeneration = 0;
    quint64 secondaryGeneration = 0;

    QWidget* surface = nullptr;
    player_sdk_qt::VideoWidget* primaryView = nullptr;
    player_sdk_qt::VideoWidget* secondaryView = nullptr;
    CompositeCanvas* composite = nullptr;
    QLabel* marker = nullptr;
    QTimer* compositeTimer = nullptr;

    Mode mode = Mode::SideBySide;
    qint64 offset = 0;
    qint64 primaryPosition = 0;
    qint64 secondaryDuration = UnknownDuration;
    qint64 secondaryPosition = 0;
    play_core::PlayerState primaryState = play_core::PlayerState::Closed;
    QString secondaryPath;
    double playbackRate = 1.0;
    double wipePosition = 0.5;
    double overlayAmount = 0.75;
    bool secondaryOpened = false;
    bool secondaryOutOfRange = false;
    bool referenceFrozen = false;
    QImage referenceImage;
    bool hasPrimaryFrame = false;
    bool hasSecondaryFrame = false;

    std::deque<Command> commands;
    std::optional<InFlight> inFlight;

    explicit Impl(ReviewCompare* owner) : q(owner)
    {
        surface = new QWidget(q);
        surface->setObjectName(QStringLiteral("compareVideoSurface"));
        surface->setAttribute(Qt::WA_OpaquePaintEvent);
        surface->setStyleSheet(QStringLiteral("background:#000000;"));
        primaryView = new player_sdk_qt::VideoWidget(surface);
        primaryView->setObjectName(QStringLiteral("comparePrimaryVideo"));
        secondaryView = new player_sdk_qt::VideoWidget(surface);
        secondaryView->setObjectName(QStringLiteral("compareSecondaryVideo"));
        composite = new CompositeCanvas(q);
        composite->setObjectName(QStringLiteral("compareComposite"));
        composite->hide();
        marker = new QLabel(q);
        marker->setObjectName(QStringLiteral("compareMarker"));
        marker->setStyleSheet(QStringLiteral(
            "QLabel { color:#f3f5f7; background:rgba(20,25,31,190); "
            "border:1px solid #687686; padding:4px 8px; }"));
        marker->hide();
        compositeTimer = new QTimer(q);
        compositeTimer->setInterval(1000 / CompositeHz);
        compositeTimer->setTimerType(Qt::CoarseTimer);
        QObject::connect(compositeTimer, &QTimer::timeout, q, [this] { renderComposite(); });
    }

    ~Impl()
    {
        if (primary)
            QObject::disconnect(primary, nullptr, q, nullptr);
        clearSecondary();
    }

    void emitError(const QString& message)
    {
        if (!message.isEmpty())
            emit q->errorOccurred(message);
    }

    bool isPlaying() const
    {
        return primaryState == play_core::PlayerState::Playing;
    }

    qint64 mappedPosition() const
    {
        if ((offset > 0 && primaryPosition > std::numeric_limits<qint64>::max() - offset)
            || (offset < 0 && primaryPosition < std::numeric_limits<qint64>::min() - offset))
            return offset < 0 ? std::numeric_limits<qint64>::min() : std::numeric_limits<qint64>::max();
        return primaryPosition + offset;
    }

    bool mappedInRange() const
    {
        const qint64 target = mappedPosition();
        return target >= 0 && (secondaryDuration <= 0 || target < secondaryDuration);
    }

    void clearSecondaryFrame()
    {
        secondaryView->setVideoFrame({});
        secondaryView->setSubtitleFrame({});
        hasSecondaryFrame = false;
    }

    void updateMarker()
    {
        if (secondaryOutOfRange) {
            marker->setText(QObject::tr("B 越界：A + 偏移不在 B 的时长范围内"));
            marker->show();
        } else if (referenceFrozen) {
            marker->setText(QObject::tr("B 参考帧（已冻结）"));
            marker->show();
        } else if (!secondaryOpened) {
            marker->setText(QObject::tr("请选择 B 素材；比较为毫秒近似同步，非帧锁"));
            marker->show();
        } else {
            marker->hide();
        }
        marker->adjustSize();
        marker->raise();
    }

    void setSecondaryOutOfRange(bool value)
    {
        secondaryOutOfRange = value;
        if (value)
            clearSecondaryFrame();
        updateMarker();
    }

    void coalesce(Command command)
    {
        if (command.type == CommandType::Seek) {
            commands.erase(std::remove_if(commands.begin(), commands.end(),
                                          [](const Command& item) {
                                              return item.type == CommandType::Seek;
                                          }), commands.end());
        } else if (command.type == CommandType::Play || command.type == CommandType::Pause) {
            commands.erase(std::remove_if(commands.begin(), commands.end(),
                                          [&command](const Command& item) {
                                              return item.type == command.type;
                                          }), commands.end());
        } else if (command.type == CommandType::Rate || command.type == CommandType::Mute) {
            commands.erase(std::remove_if(commands.begin(), commands.end(),
                                          [&command](const Command& item) {
                                              return item.type == command.type;
                                          }), commands.end());
        }
        if (commands.size() >= 32) {
            emitError(QObject::tr("B 播放请求队列已满。"));
            return;
        }
        commands.push_back(std::move(command));
        pump();
    }

    void pump()
    {
        if (!secondary || inFlight || commands.empty())
            return;
        Command command = std::move(commands.front());
        commands.pop_front();
        play_core::Request request;
        switch (command.type) {
        case CommandType::Open:
            request = secondary->open(QUrl::fromLocalFile(command.path), false);
            break;
        case CommandType::Pause:
            request = secondary->setPaused(true);
            break;
        case CommandType::Seek:
            request = secondary->seek(command.value);
            break;
        case CommandType::Play:
            request = secondary->play();
            break;
        case CommandType::Rate:
            request = secondary->setPlaybackRate(command.rate);
            break;
        case CommandType::Mute:
            request = secondary->setVolume(0.0);
            break;
        }
        if (!request) {
            discardTransport();
            emitError(requestError(request));
            QTimer::singleShot(0, q, [this] { pump(); });
            return;
        }
        inFlight = InFlight{std::move(command), request, secondaryGeneration};
    }

    void discardTransport()
    {
        commands.erase(std::remove_if(commands.begin(), commands.end(), [](const Command& item) {
            return item.type == CommandType::Pause || item.type == CommandType::Seek
                || item.type == CommandType::Play;
        }), commands.end());
    }

    void synchronize(bool resume)
    {
        if (!secondary || !secondaryOpened || referenceFrozen)
            return;
        discardTransport();
        const qint64 target = mappedPosition();
        if (!mappedInRange()) {
            setSecondaryOutOfRange(true);
            coalesce(Command{CommandType::Pause});
            return;
        }
        setSecondaryOutOfRange(false);
        if (secondary->state() == play_core::PlayerState::Ended) {
            if (!inFlight || inFlight->command.type != CommandType::Open) {
                secondaryOpened = false;
                coalesce(Command{CommandType::Open, 0, 1.0, secondaryPath});
            }
            return;
        }
        // A seek is only issued for explicit state/offset changes. While A is
        // playing, B runs on its own clock at the supplied rate; this avoids
        // turning positionChanged into a polling seek loop.
        coalesce(Command{CommandType::Pause});
        coalesce(Command{CommandType::Seek, target});
        if (resume)
            coalesce(Command{CommandType::Play});
    }

    void onSecondaryState(play_core::PlayerState state)
    {
        if (state == play_core::PlayerState::Failed)
            emitError(QObject::tr("B 播放失败。"));
    }

    void onSecondaryMediaInfo(std::shared_ptr<const play_core::MediaInfo> info)
    {
        secondaryDuration = info && !info->live && info->duration
            ? std::max<qint64>(0, info->duration->count()) : UnknownDuration;
        if (secondaryOpened)
            synchronize(isPlaying());
    }

    void onSecondaryVideo(std::shared_ptr<const play_core::VideoFrame> frame)
    {
        if (referenceFrozen) return;
        if (secondaryOutOfRange) {
            clearSecondaryFrame();
            return;
        }
        hasSecondaryFrame = static_cast<bool>(frame);
        secondaryView->setVideoFrame(std::move(frame));
    }

    void onSecondaryPosition(qint64 position)
    {
        secondaryPosition = std::max<qint64>(0, position);
        if (!referenceFrozen) secondaryView->setPosition(position);
    }

    void bindSecondary()
    {
        const quint64 generation = secondaryGeneration;
        QObject::connect(secondary, &player_sdk_qt::QtPlayer::stateChanged, q,
                         [this, generation](play_core::PlayerState state) {
            if (generation == secondaryGeneration && secondary)
                onSecondaryState(state);
        });
        QObject::connect(secondary, &player_sdk_qt::QtPlayer::mediaInfoChanged, q,
                         [this, generation](std::shared_ptr<const play_core::MediaInfo> info) {
            if (generation == secondaryGeneration && secondary)
                onSecondaryMediaInfo(std::move(info));
        });
        QObject::connect(secondary, &player_sdk_qt::QtPlayer::videoFrameChanged, q,
                         [this, generation](std::shared_ptr<const play_core::VideoFrame> frame) {
            if (generation == secondaryGeneration && secondary)
                onSecondaryVideo(std::move(frame));
        });
        QObject::connect(secondary, &player_sdk_qt::QtPlayer::subtitleChanged, q,
                         [this, generation](std::shared_ptr<const play_core::SubtitleFrame> frame) {
            if (generation == secondaryGeneration && secondary && !referenceFrozen
                && !secondaryOutOfRange)
                secondaryView->setSubtitleFrame(std::move(frame));
        });
        QObject::connect(secondary, &player_sdk_qt::QtPlayer::positionChanged, q,
                         [this, generation](qint64 position) {
            if (generation == secondaryGeneration && secondary)
                onSecondaryPosition(position);
        });
        QObject::connect(secondary, &player_sdk_qt::QtPlayer::errorOccurred, q,
                         [this, generation](const QString& message) {
            if (generation == secondaryGeneration && secondary)
                emitError(message);
        });
        QObject::connect(secondary, &player_sdk_qt::QtPlayer::ended, q,
                         [this, generation] {
            if (generation == secondaryGeneration && secondary)
                synchronize(isPlaying());
        });
        QObject::connect(secondary, &player_sdk_qt::QtPlayer::operationFinished, q,
                         [this, generation](const play_core::OperationResult& result) {
            if (generation != secondaryGeneration || !secondary || !inFlight
                || inFlight->generation != generation
                || inFlight->request.operation != result.operation
                || inFlight->request.session != result.session)
                return;
            const Command command = inFlight->command;
            inFlight.reset();
            if (result.status != play_core::OperationStatus::Succeeded) {
                discardTransport();
                emitError(resultError(result));
                if (command.type == CommandType::Open)
                    secondaryOpened = false;
            } else if (command.type == CommandType::Open) {
                const auto info = secondary->mediaInfo();
                if (!info || !info->seekable || info->live || info->width <= 0 || info->height <= 0
                    || !info->duration || info->duration->count() <= 0) {
                    commands.clear();
                    clearSecondaryFrame();
                    emitError(QObject::tr("B 必须是含视频轨道、已知时长且可定位的本地素材。"));
                    return;
                }
                secondaryOpened = true;
                coalesce(Command{CommandType::Mute});
                coalesce(Command{CommandType::Rate, 0, playbackRate});
                synchronize(isPlaying());
            }
            pump();
        });
    }

    void clearSecondary()
    {
        ++secondaryGeneration;
        commands.clear();
        inFlight.reset();
        secondaryOpened = false;
        secondaryDuration = UnknownDuration;
        secondaryPosition = 0;
        secondaryOutOfRange = false;
        referenceFrozen = false;
        referenceImage = {};
        hasSecondaryFrame = false;
        secondaryPath.clear();
        if (secondary) {
            QObject::disconnect(secondary, nullptr, q, nullptr);
            delete secondary;
            secondary = nullptr;
        }
        clearSecondaryFrame();
        secondaryView->setSubtitleFrame({});
        updateMarker();
        renderComposite();
    }

    bool setSecondary(const QString& path)
    {
        const QFileInfo info(path);
        if (!info.isFile() || !info.isReadable()) {
            emitError(QObject::tr("B 视频不存在或不可读：%1").arg(path));
            return false;
        }
        clearSecondary();
        secondaryPath = info.absoluteFilePath();
        try {
            secondary = new player_sdk_qt::QtPlayer(q);
        } catch (...) {
            secondary = nullptr;
            secondaryPath.clear();
            emitError(QObject::tr("无法初始化 B 播放器。"));
            return false;
        }
        bindSecondary();
        coalesce(Command{CommandType::Open, 0, 1.0, secondaryPath});
        updateLayout();
        return true;
    }

    void bindPrimary(player_sdk_qt::QtPlayer* player)
    {
        if (primary)
            QObject::disconnect(primary, nullptr, q, nullptr);
        ++primaryGeneration;
        primary = player;
        primaryState = play_core::PlayerState::Closed;
        primaryPosition = 0;
        hasPrimaryFrame = false;
        primaryView->setVideoFrame({});
        primaryView->setSubtitleFrame({});
        if (!primary) {
            discardTransport();
            if (secondaryOpened) coalesce(Command{CommandType::Pause});
            return;
        }
        primaryState = primary->state();
        const quint64 generation = primaryGeneration;
        QObject::connect(primary, &player_sdk_qt::QtPlayer::stateChanged, q,
                         [this, generation](play_core::PlayerState state) {
            if (generation != primaryGeneration || !primary)
                return;
            primaryState = state;
            if (state == play_core::PlayerState::Playing)
                synchronize(true);
            else if (state == play_core::PlayerState::Paused
                     || state == play_core::PlayerState::Ready
                     || state == play_core::PlayerState::Stopped
                     || state == play_core::PlayerState::Ended
                     || state == play_core::PlayerState::Failed
                     || state == play_core::PlayerState::Closed)
                synchronize(false);
        });
        QObject::connect(primary, &player_sdk_qt::QtPlayer::positionChanged, q,
                         [this, generation](qint64 position) {
            if (generation != primaryGeneration || !primary)
                return;
            primaryPosition = std::max<qint64>(0, position);
            primaryView->setPosition(position);
            // Decoder position notifications can trail an explicit seek result.
            // Only a range transition drives transport here; navigation is
            // synchronized from the completed operation below.
            if (isPlaying() && !referenceFrozen && secondaryOpened
                && secondaryOutOfRange == mappedInRange())
                synchronize(true);
        });
        QObject::connect(primary, &player_sdk_qt::QtPlayer::operationFinished, q,
                         [this, generation](const play_core::OperationResult& result) {
            if (generation != primaryGeneration || !primary
                || result.status != play_core::OperationStatus::Succeeded
                || (!result.requestedPositionMs && !result.actualPositionMs))
                return;
            // QtPlayer exposes requested seek positions on terminal results;
            // this is the explicit-seek edge, not a timer-based correction.
            primaryPosition = std::max<qint64>(0, result.actualPositionMs.value_or(result.requestedPositionMs.value_or(0)));
            primaryView->setPosition(primaryPosition);
            synchronize(isPlaying());
        });
        QObject::connect(primary, &player_sdk_qt::QtPlayer::videoFrameChanged, q,
                         [this, generation](std::shared_ptr<const play_core::VideoFrame> frame) {
            if (generation != primaryGeneration || !primary)
                return;
            hasPrimaryFrame = static_cast<bool>(frame);
            primaryView->setVideoFrame(std::move(frame));
        });
        QObject::connect(primary, &player_sdk_qt::QtPlayer::subtitleChanged, q,
                         [this, generation](std::shared_ptr<const play_core::SubtitleFrame> frame) {
            if (generation == primaryGeneration && primary)
                primaryView->setSubtitleFrame(std::move(frame));
        });
        QObject::connect(primary, &player_sdk_qt::QtPlayer::errorOccurred, q,
                         [this, generation](const QString& message) {
            if (generation == primaryGeneration && primary)
                emitError(message);
        });
        QObject::connect(primary, &QObject::destroyed, q, [this, generation] {
            if (generation == primaryGeneration) bindPrimary(nullptr);
        });
        synchronize(isPlaying());
    }

    void setMode(Mode value)
    {
        if (mode == value)
            return;
        mode = value;
        updateLayout();
        if (mode != Mode::SideBySide)
            renderComposite();
    }

    QSize captureSize() const
    {
        QSize result = surface->size();
        if (result.width() <= 0 || result.height() <= 0)
            return {};
        const double dpr = primaryView->devicePixelRatioF();
        const double pixels = double(result.width()) * result.height() * dpr * dpr;
        if (pixels > MaxCompositePixels) {
            const double scale = std::sqrt(double(MaxCompositePixels) / pixels);
            result = QSize(std::max(1, qFloor(result.width() * scale)),
                           std::max(1, qFloor(result.height() * scale)));
        }
        return result;
    }

    QImage grabView(player_sdk_qt::VideoWidget* view, const QSize& size)
    {
        if (!view || size.isEmpty())
            return {};
        const QRect old = view->geometry();
        view->setGeometry(QRect(QPoint(), size));
        const QImage image = view->grabVideoFrame();
        view->setGeometry(old);
        return image;
    }

    void renderComposite()
    {
        if (mode == Mode::SideBySide || !composite->isVisible())
            return;
        // Wipe/overlay/difference are deliberately bounded CPU readback
        // composites. VideoWidget exposes display-domain 8-bit QImage data,
        // so this is not HDR/linear-light or frame-lock comparison.
        const QSize size = captureSize();
        if (size.isEmpty())
            return;
        const QImage a = grabView(primaryView, size);
        const QImage b = referenceFrozen && !referenceImage.isNull()
            ? referenceImage : grabView(secondaryView, size);
        composite->setFrames(a, b, mode, wipePosition, overlayAmount);
    }

    void updateLayout()
    {
        const QRect r = q->rect();
        surface->setGeometry(r);
        composite->setGeometry(r);
        marker->adjustSize();
        marker->move(10, 10);
        if (mode == Mode::SideBySide) {
            composite->hide();
            if (compositeTimer)
                compositeTimer->stop();
            const int left = r.width() / 2;
            primaryView->setGeometry(0, 0, std::max(0, left), r.height());
            secondaryView->setGeometry(left, 0, std::max(0, r.width() - left), r.height());
            primaryView->show();
            secondaryView->show();
        } else {
            primaryView->setGeometry(r);
            secondaryView->setGeometry(r);
            primaryView->hide();
            secondaryView->hide();
            composite->show();
            composite->raise();
            if (compositeTimer)
                compositeTimer->start();
        }
        marker->raise();
        updateMarker();
    }
};

ReviewCompare::ReviewCompare(QWidget* parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(this))
{
    setObjectName(QStringLiteral("reviewCompare"));
    setMinimumSize(160, 90);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    impl_->updateLayout();
}

ReviewCompare::~ReviewCompare() = default;

void ReviewCompare::setPrimary(player_sdk_qt::QtPlayer* player)
{
    impl_->bindPrimary(player);
}

bool ReviewCompare::setSecondary(const QString& localPath)
{
    return impl_->setSecondary(localPath);
}

void ReviewCompare::clearSecondary()
{
    impl_->clearSecondary();
}

void ReviewCompare::setOffset(qint64 offsetMs)
{
    if (impl_->offset == offsetMs)
        return;
    impl_->offset = offsetMs;
    impl_->synchronize(impl_->isPlaying());
}

qint64 ReviewCompare::offset() const
{
    return impl_->offset;
}

void ReviewCompare::setMode(Mode mode)
{
    impl_->setMode(mode);
}

ReviewCompare::Mode ReviewCompare::mode() const
{
    return impl_->mode;
}

void ReviewCompare::captureReference()
{
    if (impl_->secondaryOutOfRange)
        return;
    const QSize size = impl_->captureSize();
    const QImage image = impl_->grabView(impl_->secondaryView, size);
    if (image.isNull())
        return;
    impl_->referenceImage = image;
    impl_->referenceFrozen = true;
    impl_->discardTransport();
    if (impl_->secondary && impl_->secondaryOpened)
        impl_->coalesce(Impl::Command{Impl::CommandType::Pause});
    impl_->updateMarker();
    impl_->renderComposite();
}

void ReviewCompare::clearReference()
{
    if (!impl_->referenceFrozen)
        return;
    impl_->referenceFrozen = false;
    impl_->referenceImage = {};
    impl_->updateMarker();
    impl_->synchronize(impl_->isPlaying());
    impl_->renderComposite();
}

void ReviewCompare::setPlaybackRate(double rate)
{
    if (!std::isfinite(rate))
        return;
    impl_->playbackRate = std::clamp(rate, 0.1, 2.0);
    if (impl_->secondary && impl_->secondaryOpened)
        impl_->coalesce(Impl::Command{Impl::CommandType::Rate, 0, impl_->playbackRate});
}

void ReviewCompare::setWipePosition(double position)
{
    if (!std::isfinite(position))
        return;
    impl_->wipePosition = std::clamp(position, 0.0, 1.0);
    impl_->renderComposite();
}

double ReviewCompare::wipePosition() const
{
    return impl_->wipePosition;
}

void ReviewCompare::setOverlayAmount(double amount)
{
    if (!std::isfinite(amount))
        return;
    impl_->overlayAmount = std::clamp(amount, 0.0, 1.0);
    impl_->renderComposite();
}

double ReviewCompare::overlayAmount() const
{
    return impl_->overlayAmount;
}

player_sdk_qt::QtPlayer* ReviewCompare::primary() const
{
    return impl_->primary;
}

player_sdk_qt::QtPlayer* ReviewCompare::secondary() const
{
    return impl_->secondary;
}

player_sdk_qt::VideoWidget* ReviewCompare::primaryView() const
{
    return impl_->primaryView;
}

player_sdk_qt::VideoWidget* ReviewCompare::secondaryView() const
{
    return impl_->secondaryView;
}

QString ReviewCompare::secondaryPath() const
{
    return impl_->secondaryPath;
}

qint64 ReviewCompare::secondaryDurationMs() const
{
    return impl_->secondaryDuration;
}

bool ReviewCompare::secondaryOutOfRange() const
{
    return impl_->secondaryOutOfRange;
}

bool ReviewCompare::hasReference() const
{
    return impl_->referenceFrozen;
}

bool ReviewCompare::primaryPlaying() const
{
    return impl_->isPlaying();
}

bool ReviewCompare::isFrameLocked() const
{
    return false;
}

void ReviewCompare::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    impl_->updateLayout();
    if (impl_->mode != Mode::SideBySide)
        impl_->renderComposite();
}

} // namespace review
