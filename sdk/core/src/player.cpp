#include <play_core/player.h>
#include <play_core/network_runtime.h>
#include "runtime_manager.h"
#include "videoctl.h"
#include "network_input.h"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <map>
#include <thread>
#include <atomic>
#include <cmath>
#include <stdexcept>

namespace play_core {
NetworkRuntime::NetworkRuntime() { if (!RuntimeManager::instance().acquireNetwork()) throw std::runtime_error("FFmpeg network initialization failed"); }
NetworkRuntime::~NetworkRuntime() { RuntimeManager::instance().releaseNetwork(); }
struct Subscription::State {
    std::mutex mutex;
    std::condition_variable cv;
    std::function<void(const PlayerEvent&)> callback;
    bool active{true};
    unsigned running{0};
};
Subscription::Subscription() noexcept = default;
Subscription::Subscription(std::shared_ptr<State> s) : state_(std::move(s)) {}
Subscription::~Subscription() { reset(); }
Subscription::Subscription(Subscription&& other) noexcept = default;
Subscription& Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) { reset(); state_ = std::move(other.state_); }
    return *this;
}
void Subscription::reset() noexcept {
    auto s = std::move(state_);
    if (!s) return;
    std::unique_lock lock(s->mutex);
    s->active = false;
    s->cv.wait(lock, [&] { return s->running == 0; });
    s->callback = {};
}
namespace {
PlayerError mediaError(::PlaybackError error) {
    ErrorCode code = ErrorCode::Media; const char* message = "Media session failed";
    switch (error) {
    case ::PlaybackError::Timeout: code = ErrorCode::Timeout; message = "Media input timed out"; break;
    case ::PlaybackError::Authentication: code = ErrorCode::Authentication; message = "Media authentication failed"; break;
    case ::PlaybackError::NotFound: code = ErrorCode::NotFound; message = "Media source not found"; break;
    case ::PlaybackError::UnsupportedProtocol: code = ErrorCode::UnsupportedProtocol; message = "Unsupported media protocol"; break;
    case ::PlaybackError::NetworkUnavailable: code = ErrorCode::NetworkUnavailable; message = "Media network unavailable"; break;
    case ::PlaybackError::ConnectionLost: code = ErrorCode::ConnectionLost; message = "Media connection lost"; break;
    case ::PlaybackError::InvalidMedia: code = ErrorCode::InvalidMedia; message = "Invalid media input"; break;
    case ::PlaybackError::DecoderFailure: code = ErrorCode::DecoderFailure; message = "Media decoder failed"; break;
    default: break;
    }
    return {code,message,static_cast<int>(error)};
}
::MediaSource nativeSource(const MediaSource& s) {
    ::MediaSource n;
    n.location = s.location; n.enableAudio = s.enableAudio; n.boundedBuffer = true;
    n.network.connectTimeout = s.network.connectTimeout; n.network.readTimeout = s.network.readTimeout;
    n.network.analyzeDuration = s.network.analyzeDuration; n.network.probeSize = s.network.probeSize;
    n.network.rtspTransport = s.network.rtspTransport == RtspTransport::Tcp ? ::RtspTransport::Tcp : ::RtspTransport::Udp;
    n.network.userAgent = s.network.userAgent; n.network.headers = s.network.headers;
    n.network.maxReconnectAttempts = s.network.maxReconnectAttempts;
    n.network.initialReconnectDelay = s.network.initialReconnectDelay; n.network.maxReconnectDelay = s.network.maxReconnectDelay;
    n.network.stablePlaybackReset = s.network.stablePlaybackReset; n.network.reconnect = s.network.reconnect;
    return n;
}
std::shared_ptr<const MediaInfo> publicInfo(const ::MediaInfo& n) {
    auto s = std::make_shared<MediaInfo>();
    s->networkSource = n.networkSource; s->live = n.live; s->seekable = n.seekable; s->duration = n.duration;
    s->width = n.width; s->height = n.height; s->rotationDegrees = n.rotationDegrees;
    s->sampleAspectRatio = {n.sampleAspectRatio.num, n.sampleAspectRatio.den};
    for (const auto& t : n.tracks) s->tracks.push_back({t.streamIndex,
        t.type == AVMEDIA_TYPE_AUDIO ? MediaType::Audio : t.type == AVMEDIA_TYPE_VIDEO ? MediaType::Video :
        t.type == AVMEDIA_TYPE_SUBTITLE ? MediaType::Subtitle : MediaType::Unknown,
        t.language, t.title, t.codec, t.isDefault, t.isForced});
    return s;
}
std::shared_ptr<const VideoFrame> publicFrame(const ::VideoFrame& n) {
    auto f = std::make_shared<VideoFrame>();
    f->format = n.format == ::VideoFrameFormat::Bgra32 ? VideoFrameFormat::Bgra32 : VideoFrameFormat::Yuv420P;
    f->width = n.width; f->height = n.height; f->bytesPerLine = n.bytesPerLine;
    f->sampleAspectRatio = {n.sampleAspectRatio.num, n.sampleAspectRatio.den}; f->rotationDegrees = n.rotationDegrees;
    f->unsupportedHdrTransfer = n.unsupportedHdrTransfer;
    f->colorRange = n.colorRange == AVCOL_RANGE_JPEG ? ColorRange::Full : n.colorRange == AVCOL_RANGE_MPEG ? ColorRange::Limited : ColorRange::Unknown;
    f->colorSpace = n.colorSpace == AVCOL_SPC_BT709 ? ColorSpace::Bt709 : n.colorSpace == AVCOL_SPC_BT2020_NCL ? ColorSpace::Bt2020 :
        n.colorSpace == AVCOL_SPC_SMPTE170M || n.colorSpace == AVCOL_SPC_BT470BG ? ColorSpace::Bt601 : ColorSpace::Unknown;
    f->bgra = n.bgra; f->timestampMs = n.timestampMs;
    // Copy the planes: published storage must never alias reusable decoder buffers.
    for (std::size_t i = 0; i < n.planes.size(); ++i) {
        const auto& p = n.planes[i];
        if (!p.data || p.stride <= 0 || p.height <= 0) continue;
        auto bytes = std::shared_ptr<std::uint8_t>(new std::uint8_t[static_cast<std::size_t>(p.stride) * p.height], std::default_delete<std::uint8_t[]>());
        std::copy_n(p.data.get(), static_cast<std::size_t>(p.stride) * p.height, bytes.get());
        f->planes[i] = {bytes, p.stride, p.height};
    }
    return f;
}
}
struct Player::Impl {
    enum class Command { Hardware, Open, Play, Pause, Seek, Stop, Close, Volume, Rate, Audio, Subtitle };
    struct Job { Command command; Request request; MediaSource source; double value{0}; std::optional<int> track; std::int64_t position{0}; };
    PlayerOptions options;
    std::shared_ptr<VideoCtl> ctl;
    std::vector<sigslot::scoped_connection> connections;
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::function<void()>> jobs;
    bool quitting{false};
    OperationId nextOperation{0};
    SessionId nextSession{0}, acceptedSession{0}, session{0};
    std::atomic<SessionId> visibleSession{0};
    std::size_t outstanding{0};
    std::map<OperationId, Request> pending;
    std::optional<Request> opening;
    std::shared_ptr<const MediaInfo> info;
    PlayerState state{PlayerState::Closed};
    bool stopped{false};
    PlayerState reconnectState{PlayerState::Ready};
    std::vector<Job> deferred;
    std::mutex deliveryMutex;
    std::condition_variable deliveryCv;
    std::deque<PlayerEvent> events;
    std::vector<std::weak_ptr<Subscription::State>> subscribers;
    bool deliveryQuitting{false};
    std::thread worker, dispatcher;

    explicit Impl(PlayerOptions o) : options(o), ctl(VideoCtl::MakeInstance()) {
        if (!ctl) throw std::runtime_error("Player audio/network initialization failed");
        ctl->set_play_loop_policy(o.loop ? LOOP_SINGLE : LOOP_ALL);
        ctl->set_hardware_decode_preference(static_cast<HardwareDecodePreference>(o.hardwareDecode));
        dispatcher = std::thread([this] { dispatch(); });
        try { worker = std::thread([this] { run(); }); }
        catch (...) { { std::lock_guard lock(deliveryMutex); deliveryQuitting = true; } deliveryCv.notify_one(); dispatcher.join(); throw; }
    }
    ~Impl() {
        { std::lock_guard lock(mutex); quitting = true; }
        cv.notify_one();
        worker.join();
        { std::lock_guard lock(deliveryMutex); deliveryQuitting = true; events.clear(); }
        deliveryCv.notify_one(); dispatcher.join();
    }
    void post(std::function<void()> f) {
        { std::lock_guard lock(mutex); if (quitting) return; jobs.push_back(std::move(f)); }
        cv.notify_one();
    }
    Request submit(Job j) {
        std::lock_guard lock(mutex);
        if (quitting || outstanding >= 128) return {0, acceptedSession, {ErrorCode::Capacity, "Player request capacity exceeded"}};
        if (j.command == Command::Open) { acceptedSession = ++nextSession; visibleSession.store(acceptedSession); }
        j.request = {++nextOperation, acceptedSession, {}};
        const auto r = j.request;
        jobs.push_back([this, j = std::move(j)] { pending.emplace(j.request.operation, j.request); execute(j); });
        ++outstanding; cv.notify_one(); return r;
    }
    void emit(PlayerEvent e) {
        std::lock_guard lock(deliveryMutex);
        // Transient notifications are mailboxes, never one queued event per frame.
        const bool transient = e.type == PlayerEvent::Type::Video || e.type == PlayerEvent::Type::Position ||
            e.type == PlayerEvent::Type::Subtitle || e.type == PlayerEvent::Type::MediaInfo ||
            (options.loop && e.type == PlayerEvent::Type::Ended);
        if (transient) {
            std::erase_if(events,[&](const auto& old) { return old.type == e.type && old.session != e.session; });
            auto it = std::find_if(events.begin(), events.end(), [&](const auto& old) { return old.type == e.type && old.session == e.session; });
            if (it != events.end()) { *it = std::move(e); deliveryCv.notify_one(); return; }
        }
        events.push_back(std::move(e)); deliveryCv.notify_one();
    }
    void setState(PlayerState s) { state = s; PlayerEvent e; e.session = session; e.state = s; emit(std::move(e)); }
    void finish(Request r, OperationStatus status = OperationStatus::Succeeded, PlayerError error = {},
                std::optional<std::int64_t> requested = {}, std::optional<std::int64_t> actual = {}) {
        if (!pending.erase(r.operation)) return;
        PlayerEvent e; e.type = PlayerEvent::Type::Operation; e.session = r.session;
        e.operation = {r.operation, r.session, status, std::move(error), requested, actual}; emit(std::move(e));
    }
    void fail(Request r, const char* message) { finish(r, OperationStatus::Failed, {ErrorCode::InvalidState, message}); }
    void cancelAll(OperationStatus status, OperationId except = 0) {
        auto copy = pending;
        for (const auto& [id, r] : copy) if (id != except)
            finish(r,status,status == OperationStatus::Failed ? PlayerError{ErrorCode::Media,"Operation interrupted by media failure or reconnect"} :
                PlayerError{ErrorCode::Cancelled,"Session closed or replaced"});
        opening.reset(); deferred.clear();
    }
    void teardown(OperationStatus status, OperationId except) {
        ctl->OnStopAndWait(); connections.clear();
        cancelAll(status, except); info.reset(); stopped = false;
    }
    void connect(SessionId sid) {
        connections.emplace_back(ctl->SigMediaInfo.connect([this, sid](const ::MediaInfo& n) {
            auto snapshot = publicInfo(n);
            post([this, sid, snapshot] { if (sid != session) return; info = snapshot;
                PlayerEvent e; e.type = PlayerEvent::Type::MediaInfo; e.session = sid; e.mediaInfo = snapshot; emit(std::move(e)); });
        }));
        connections.emplace_back(ctl->SigPlaybackStatus.connect([this, sid](const ::PlaybackStatus& s) {
            post([this, sid, s] {
                if (sid != session) return;
                if (s.state == ::PlaybackState::Playing && opening) {
                    setState(PlayerState::Ready); finish(*opening); opening.reset();
                    auto queued = std::move(deferred); deferred.clear();
                    for (const auto& j : queued) execute(j);
                } else if (s.state == ::PlaybackState::Reconnecting) {
                    if (state != PlayerState::Reconnecting) reconnectState = state;
                    if (!opening) cancelAll(OperationStatus::Failed);
                    setState(PlayerState::Reconnecting);
                } else if (s.state == ::PlaybackState::Playing && state == PlayerState::Reconnecting) {
                    const auto target = reconnectState;
                    ctl->SdkPause(target != PlayerState::Playing, [this, sid, target](bool ok) {
                        post([this, sid, target, ok] { if (sid == session && ok) setState(target); });
                    });
                }
            });
        }));
        connections.emplace_back(ctl->SigSessionFailure.connect([this, sid](::PlaybackError error) {
            post([this, sid, error] {
                if (sid != session) return;
                setState(PlayerState::Failed);
                PlayerEvent e; e.type = PlayerEvent::Type::Error; e.session = sid;
                e.error = mediaError(error);
                emit(std::move(e)); cancelAll(OperationStatus::Failed);
            });
        }));
        connections.emplace_back(ctl->SigPositionMs.connect([this, sid](int64_t position) {
            PlayerEvent e; e.type = PlayerEvent::Type::Position; e.session = sid; e.positionMs = position; emit(std::move(e));
        }));
        connections.emplace_back(ctl->SigVideoFrame.connect([this, sid](std::shared_ptr<::VideoFrame> f) {
            if (!f || sid != visibleSession.load()) return;
            PlayerEvent e; e.type = PlayerEvent::Type::Video; e.session = sid; e.video = publicFrame(*f); emit(std::move(e));
        }));
        connections.emplace_back(ctl->SigSubtitleFrame.connect([this, sid](std::shared_ptr<const ::SubtitleFrame> f) {
            PlayerEvent e; e.type = PlayerEvent::Type::Subtitle; e.session = sid;
            if (f) {
                auto sub = std::make_shared<SubtitleFrame>(); sub->startMs = static_cast<int64_t>(f->startSeconds * 1000) - f->timestampOffsetMs;
                sub->endMs = static_cast<int64_t>(f->endSeconds * 1000) - f->timestampOffsetMs; sub->assOrText = f->assOrText;
                sub->assHeader = f->assHeader; sub->assPacket = f->assPacket;
                for (const auto& b : f->bitmaps) sub->bitmaps.push_back({b.x,b.y,b.width,b.height,b.stride,b.bgra});
                e.subtitle = sub;
            }
            emit(std::move(e));
        }));
        connections.emplace_back(ctl->SigNaturalEnd.connect([this, sid] { post([this, sid] {
            if (sid != session) return;
            if (!options.loop) { setState(PlayerState::Ended); cancelAll(OperationStatus::Cancelled); }
            PlayerEvent e; e.type = PlayerEvent::Type::Ended; e.session = sid; emit(std::move(e));
        }); }));
    }
    void pause(Request r, bool paused, PlayerState target) {
        ctl->SdkPause(paused, [this, r, target](bool ok) { post([this, r, target, ok] {
            if (!pending.contains(r.operation) || r.session != session) return;
            if (ok) { setState(target); finish(r); } else fail(r, "No active media");
        }); });
    }
    void execute(const Job& j) {
        const auto r = j.request;
        if (!pending.contains(r.operation)) return;
        if (j.command == Command::Open) {
            teardown(OperationStatus::Superseded, r.operation); session = r.session;
            setState(PlayerState::Opening); opening = r; connect(session);
            if (!ctl->OpenPaused(nativeSource(j.source))) {
                setState(PlayerState::Failed); finish(r, OperationStatus::Failed, {ErrorCode::Media, "Could not open media"}); opening.reset();
            }
            return;
        }
        if (j.command == Command::Close) {
            session = r.session;
            setState(PlayerState::Closing); teardown(OperationStatus::Cancelled, r.operation);
            setState(PlayerState::Closed); finish(r);
            PlayerEvent e; e.type = PlayerEvent::Type::Closed; e.session = r.session; emit(std::move(e));
            session = 0; return;
        }
        if (j.command == Command::Hardware) { ctl->set_hardware_decode_preference(static_cast<HardwareDecodePreference>(static_cast<int>(j.value))); finish(r); return; }
        if (j.command == Command::Volume) { ctl->OnPlayVolume(j.value); finish(r); return; }
        if (j.command == Command::Rate) { ctl->set_play_speed(j.value); finish(r); return; }
        if (r.session != session) { fail(r, "Media session replaced"); return; }
        if (opening) { deferred.push_back(j); return; }
        if (!info || state == PlayerState::Closed || state == PlayerState::Failed || state == PlayerState::Ended) { fail(r, "No ready media"); return; }
        switch (j.command) {
        case Command::Play:
            if (stopped && info->seekable) {
                ctl->SdkSeek(0, [this, r](int error, int64_t) { post([this, r, error] {
                    if (!pending.contains(r.operation)) return;
                    if (error < 0) fail(r, "Restart seek failed"); else { stopped = false; pause(r, false, PlayerState::Playing); }
                }); });
            } else { stopped = false; pause(r, false, PlayerState::Playing); }
            break;
        case Command::Pause: pause(r, j.value != 0, j.value != 0 ? PlayerState::Paused : PlayerState::Playing); break;
        case Command::Stop: stopped = true; pause(r, true, PlayerState::Stopped); break;
        case Command::Seek:
            if (!info->seekable) { fail(r, "Media is not seekable"); break; }
            ctl->SdkSeek(j.position, [this, r, requested = j.position](int error, int64_t actual) { post([this, r, requested, error, actual] {
                finish(r, error < 0 ? OperationStatus::Failed : OperationStatus::Succeeded,
                    error < 0 ? PlayerError{ErrorCode::Media, "Demuxer seek failed or busy", error} : PlayerError{}, requested,
                    error < 0 ? std::nullopt : std::optional<int64_t>{actual});
            }); });
            break;
        case Command::Audio: case Command::Subtitle:
            ctl->SdkTrack(j.command == Command::Audio, j.track, [this, r](bool ok) { post([this, r, ok] {
                if (ok) finish(r); else fail(r, "Track unavailable or decoder failed");
            }); }); break;
        default: break;
        }
    }
    void run() noexcept {
        for (;;) {
            std::function<void()> job;
            { std::unique_lock lock(mutex); cv.wait(lock, [&] { return quitting || !jobs.empty(); });
                if (quitting) break; job = std::move(jobs.front()); jobs.pop_front(); }
            try { job(); } catch (...) {
                try { setState(PlayerState::Failed); cancelAll(OperationStatus::Failed); } catch (...) {}
            }
        }
        ctl->OnStopAndWait(); connections.clear();
    }
    void dispatch() noexcept {
        for (;;) {
            PlayerEvent event;
            std::vector<std::shared_ptr<Subscription::State>> targets;
            { std::unique_lock lock(deliveryMutex); deliveryCv.wait(lock, [&] { return deliveryQuitting || !events.empty(); });
                if (deliveryQuitting) return;
                event = std::move(events.front()); events.pop_front();
                std::erase_if(subscribers, [](const auto& s) { return s.expired(); });
                for (auto& w : subscribers) if (auto s = w.lock()) targets.push_back(std::move(s)); }
            const bool terminal = event.type == PlayerEvent::Type::Operation;
            if (terminal || event.session == visibleSession.load()) {
                for (const auto& s : targets) {
                    { std::lock_guard lock(s->mutex); if (!s->active) continue; ++s->running; }
                    try { s->callback(event); } catch (...) { /* Host exceptions never escape the dispatcher. */ }
                    { std::lock_guard lock(s->mutex); --s->running; s->cv.notify_all(); }
                }
            }
            if (terminal) { std::lock_guard lock(mutex); --outstanding; }
        }
    }
};
Player::Player(PlayerOptions o) : impl_(std::make_unique<Impl>(o)) {}
Player::~Player() = default;
Subscription Player::subscribe(std::function<void(const PlayerEvent&)> callback) {
    if (!callback) return {};
    auto s = std::make_shared<Subscription::State>(); s->callback = std::move(callback);
    std::lock_guard lock(impl_->deliveryMutex);
    std::erase_if(impl_->subscribers, [](const auto& w) { return w.expired(); });
    if (impl_->subscribers.size() >= 32) throw std::length_error("Player supports at most 32 subscriptions");
    impl_->subscribers.push_back(s); return Subscription{s};
}
Request Player::open(MediaSource source) {
    const auto validation = ValidateMediaSource(nativeSource(source));
    if (!validation.ok || source.network.maxReconnectAttempts > 32)
        return {0,0,{ErrorCode::InvalidArgument, "Invalid media source or reconnect budget"}};
    Impl::Job j{}; j.command = Impl::Command::Open; j.source = std::move(source); return impl_->submit(std::move(j));
}
Request Player::play() { Impl::Job j{}; j.command = Impl::Command::Play; return impl_->submit(std::move(j)); }
Request Player::setPaused(bool paused) { Impl::Job j{}; j.command = Impl::Command::Pause; j.value = paused; return impl_->submit(std::move(j)); }
Request Player::stop() { Impl::Job j{}; j.command = Impl::Command::Stop; return impl_->submit(std::move(j)); }
Request Player::close() { Impl::Job j{}; j.command = Impl::Command::Close; return impl_->submit(std::move(j)); }
Request Player::seek(int64_t ms) {
    if (ms < 0 || ms > INT64_MAX / 2000) return {0,0,{ErrorCode::InvalidArgument,"Seek position out of range"}};
    Impl::Job j{}; j.command = Impl::Command::Seek; j.position = ms; return impl_->submit(std::move(j));
}
Request Player::setHardwareDecode(HardwareDecode preference) {
    if (preference != HardwareDecode::Auto && preference != HardwareDecode::Disabled && preference != HardwareDecode::D3D11VA)
        return {0,0,{ErrorCode::InvalidArgument,"Unknown hardware preference"}};
    Impl::Job j{}; j.command = Impl::Command::Hardware; j.value = static_cast<int>(preference); return impl_->submit(std::move(j));
}
Request Player::setVolume(double v) {
    if (!std::isfinite(v) || v < 0 || v > 1) return {0,0,{ErrorCode::InvalidArgument,"Volume must be in [0,1]"}};
    Impl::Job j{}; j.command = Impl::Command::Volume; j.value = v; return impl_->submit(std::move(j));
}
Request Player::setPlaybackRate(double v) {
    if (!std::isfinite(v) || v < .1 || v > 2) return {0,0,{ErrorCode::InvalidArgument,"Rate must be in [0.1,2]"}};
    Impl::Job j{}; j.command = Impl::Command::Rate; j.value = v; return impl_->submit(std::move(j));
}
Request Player::selectAudioTrack(int id) { Impl::Job j{}; j.command = Impl::Command::Audio; j.track = id; return impl_->submit(std::move(j)); }
Request Player::selectSubtitleTrack(std::optional<int> id) { Impl::Job j{}; j.command = Impl::Command::Subtitle; j.track = id; return impl_->submit(std::move(j)); }
}
