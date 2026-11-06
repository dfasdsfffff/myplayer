#include <player_sdk_qt/qt_player.h>
#include <QThread>
#include <QPointer>
#include <algorithm>
#include <deque>
#include <mutex>
#include <set>
namespace player_sdk_qt {
struct QtPlayer::Impl {
    play_core::Player core;
    play_core::Subscription subscription;
    std::mutex mutex;
    std::deque<play_core::PlayerEvent> events;
    bool scheduled{false};
    play_core::SessionId session{0};
    play_core::PlayerState state{play_core::PlayerState::Closed};
    std::shared_ptr<const play_core::MediaInfo> info;
    std::set<play_core::OperationId> pending;
};
QtPlayer::QtPlayer(QObject* parent) : QObject(parent), impl_(std::make_unique<Impl>()) {
    impl_->subscription = impl_->core.subscribe([this](const play_core::PlayerEvent& e) {
        std::lock_guard lock(impl_->mutex);
        using T = play_core::PlayerEvent::Type;
        if (e.type == T::Video || e.type == T::Subtitle || e.type == T::Position || e.type == T::MediaInfo) {
            std::erase_if(impl_->events,[&](const auto& old) { return old.type == e.type && old.session != e.session; });
            auto it = std::find_if(impl_->events.begin(), impl_->events.end(), [&](const auto& old) { return old.type == e.type && old.session == e.session; });
            if (it != impl_->events.end()) *it = e; else impl_->events.push_back(e);
        } else impl_->events.push_back(e);
        if (!impl_->scheduled) {
            impl_->scheduled = true;
            QMetaObject::invokeMethod(this, [this] { drain(); }, Qt::QueuedConnection);
        }
    });
}
QtPlayer::~QtPlayer() { impl_->subscription.reset(); }
void QtPlayer::drain() {
    QPointer<QtPlayer> self(this);
    std::deque<play_core::PlayerEvent> events;
    { std::lock_guard lock(impl_->mutex); events.swap(impl_->events); impl_->scheduled = false; }
    using T = play_core::PlayerEvent::Type;
    for (const auto& e : events) {
        if (e.type == T::Operation) { impl_->pending.erase(e.operation.operation); emit operationFinished(e.operation); if (!self) return; continue; }
        if (e.session != impl_->session) continue;
        switch (e.type) {
        case T::State: impl_->state = e.state; emit stateChanged(e.state); break;
        case T::MediaInfo: impl_->info = e.mediaInfo; emit mediaInfoChanged(e.mediaInfo); break;
        case T::Position: emit positionChanged(e.positionMs); break;
        case T::Video: emit videoFrameChanged(e.video); break;
        case T::Subtitle: emit subtitleChanged(e.subtitle); break;
        case T::Error: emit errorDetails(e.error); if (!self) return; emit errorOccurred(QString::fromUtf8(e.error.message)); break;
        case T::Ended: emit ended(); break;
        case T::Closed:
            impl_->info.reset(); emit videoFrameChanged({}); if (!self) return;
            emit subtitleChanged({}); if (!self) return; emit closed(); break;
        default: break;
        }
        if (!self) return;
    }
}
play_core::Request QtPlayer::track(play_core::Request r) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (r) impl_->pending.insert(r.operation);
    else { QPointer<QtPlayer> self(this); emit errorDetails(r.error); if (self) emit errorOccurred(QString::fromUtf8(r.error.message)); }
    return r;
}
// Check the Qt-side budget before Core accepts another operation. It remains
// occupied until the QObject thread actually consumes the terminal result.
#define QT_SDK_GUARD() Q_ASSERT(QThread::currentThread() == thread()); if (impl_->pending.size() >= 128) return {0,impl_->session,{play_core::ErrorCode::Capacity,"Qt delivery capacity exceeded"}}
play_core::Request QtPlayer::open(const QUrl& url, bool audio) {
    play_core::MediaSource s; s.location = (url.isLocalFile() ? url.toLocalFile() : url.toString(QUrl::FullyEncoded)).toUtf8().toStdString();
    s.enableAudio = audio; return open(std::move(s));
}
play_core::Request QtPlayer::open(play_core::MediaSource source) {
    QT_SDK_GUARD(); auto r = track(impl_->core.open(std::move(source)));
    if (r) { impl_->session = r.session; impl_->info.reset(); emit videoFrameChanged({}); emit subtitleChanged({}); }
    return r;
}
play_core::Request QtPlayer::play() { QT_SDK_GUARD(); return track(impl_->core.play()); }
play_core::Request QtPlayer::setPaused(bool b) { QT_SDK_GUARD(); return track(impl_->core.setPaused(b)); }
play_core::Request QtPlayer::seek(qint64 ms) { QT_SDK_GUARD(); return track(impl_->core.seek(ms)); }
play_core::Request QtPlayer::stop() { QT_SDK_GUARD(); return track(impl_->core.stop()); }
play_core::Request QtPlayer::close() { QT_SDK_GUARD(); return track(impl_->core.close()); }
play_core::Request QtPlayer::setHardwareDecode(play_core::HardwareDecode p) { QT_SDK_GUARD(); return track(impl_->core.setHardwareDecode(p)); }
play_core::Request QtPlayer::setVolume(double v) { QT_SDK_GUARD(); auto r = track(impl_->core.setVolume(v)); if (r) emit volumeChanged(v); return r; }
play_core::Request QtPlayer::setPlaybackRate(double v) { QT_SDK_GUARD(); return track(impl_->core.setPlaybackRate(v)); }
play_core::Request QtPlayer::selectAudioTrack(int id) { QT_SDK_GUARD(); return track(impl_->core.selectAudioTrack(id)); }
play_core::Request QtPlayer::selectSubtitleTrack(std::optional<int> id) { QT_SDK_GUARD(); return track(impl_->core.selectSubtitleTrack(id)); }
play_core::PlayerState QtPlayer::state() const { return impl_->state; }
std::shared_ptr<const play_core::MediaInfo> QtPlayer::mediaInfo() const { return impl_->info; }
#undef QT_SDK_GUARD
}
