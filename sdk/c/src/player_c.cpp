#include <player_sdk_c/player.h>
#include <play_core/player.h>
#include "enum_contract.h"
#include <algorithm>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>

struct ps_event {
    const play_core::PlayerEvent* value;
    std::unique_ptr<play_core::PlayerEvent> owned;
    explicit ps_event(const play_core::PlayerEvent& e) noexcept : value(&e) {}
};
struct ps_player {
    play_core::Player core;
    play_core::Subscription subscription; // destroyed before core
    explicit ps_player(play_core::PlayerOptions options) : core(options) {}
};
namespace {
thread_local ps_player* callbackPlayer = nullptr;
struct CallbackScope {
    ps_player* previous;
    explicit CallbackScope(ps_player* p) noexcept : previous(callbackPlayer) { callbackPlayer = p; }
    ~CallbackScope() { callbackPlayer = previous; }
};
void error(ps_error_v1& out, int32_t code, const char* text, int native = 0) noexcept {
    out = {}; out.code = code; out.native_code = native;
    if (!text) return;
    auto n = std::min(std::strlen(text), sizeof(out.message) - 1);
    while (n && (static_cast<unsigned char>(text[n]) & 0xc0) == 0x80) --n;
    std::memcpy(out.message, text, n);
}
void error(ps_error_v1& out, const play_core::PlayerError& e) noexcept {
    error(out, static_cast<int32_t>(e.code), e.message.c_str(), e.nativeCode);
}
bool boolean(int32_t b) noexcept { return b == 0 || b == 1; }
bool hardware(int32_t h) noexcept { return h >= PS_HARDWARE_AUTO && h <= PS_HARDWARE_D3D11VA; }
template<class F>
int32_t request(ps_player* player, ps_request_v1* out, F&& f) noexcept {
    if (!out) return PS_ERROR_INVALID_ARGUMENT;
    *out = {};
    if (!player) { error(out->error, PS_ERROR_INVALID_ARGUMENT, "NULL player"); return out->error.code; }
    try {
        const auto r = f(player->core);
        out->operation = r.operation; out->session = r.session; error(out->error, r.error);
        return out->error.code;
    } catch (const std::invalid_argument& e) { error(out->error, PS_ERROR_INVALID_ARGUMENT, e.what()); }
      catch (const std::exception& e) { error(out->error, PS_ERROR_INTERNAL, e.what()); }
      catch (...) { error(out->error, PS_ERROR_INTERNAL, "Unhandled SDK exception"); }
    return out->error.code;
}
void require(bool b) { if (!b) throw std::invalid_argument("Invalid C ABI argument"); }
template<class T> bool output(const ps_event* e, T* out) noexcept {
    if (!out) return false;
    *out = {};
    return e && e->value;
}
}

extern "C" {
uint32_t PS_CALL ps_abi_version(void) { return PS_ABI_VERSION; }
const char* PS_CALL ps_version(void) { return play_core::Version; }
int32_t PS_CALL ps_player_options_init_v1(ps_player_options_v1* o) {
    if (!o) return PS_ERROR_INVALID_ARGUMENT;
    *o = {}; o->struct_size = sizeof(*o); o->hardware_decode = PS_HARDWARE_AUTO;
    return PS_OK;
}
int32_t PS_CALL ps_source_init_v1(ps_source_v1* s) {
    if (!s) return PS_ERROR_INVALID_ARGUMENT;
    *s = {}; s->struct_size = sizeof(*s); s->enable_audio = 1;
    s->connect_timeout_ms = 10000; s->read_timeout_ms = 15000;
    s->analyze_duration_us = 5000000; s->probe_size = 5 * 1024 * 1024;
    s->user_agent = "PlayerSDK/1.0"; s->max_reconnect_attempts = 5;
    s->initial_reconnect_delay_ms = 1000; s->max_reconnect_delay_ms = 15000;
    s->stable_playback_reset_seconds = 30; s->reconnect = 1;
    return PS_OK;
}
int32_t PS_CALL ps_player_create_v1(const ps_player_options_v1* o, ps_event_callback cb,
    void* user, ps_player_handle* out, ps_error_v1* err) {
    if (err) *err = {};
    if (!out) { if (err) error(*err, PS_ERROR_INVALID_ARGUMENT, "NULL output handle"); return PS_ERROR_INVALID_ARGUMENT; }
    *out = nullptr;
    try {
        play_core::PlayerOptions options;
        if (o) {
            require(o->struct_size == sizeof(*o) && hardware(o->hardware_decode) && boolean(o->loop));
            options.hardwareDecode = static_cast<play_core::HardwareDecode>(o->hardware_decode);
            options.loop = o->loop != 0;
        }
        auto p = std::make_unique<ps_player>(options);
        if (cb) p->subscription = p->core.subscribe([player = p.get(), cb, user](const auto& event) {
            CallbackScope scope(player);
            ps_event view(event);
            try { cb(user, &view); } catch (...) { /* isolate C++ clients too */ }
        });
        *out = p.release(); return PS_OK;
    } catch (const std::invalid_argument& e) { if (err) error(*err, PS_ERROR_INVALID_ARGUMENT, e.what()); return PS_ERROR_INVALID_ARGUMENT; }
      catch (const std::exception& e) { if (err) error(*err, PS_ERROR_INTERNAL, e.what()); return PS_ERROR_INTERNAL; }
      catch (...) { if (err) error(*err, PS_ERROR_INTERNAL, "Unhandled SDK exception"); return PS_ERROR_INTERNAL; }
}
int32_t PS_CALL ps_player_destroy(ps_player_handle p) {
    if (p && p == callbackPlayer) return PS_ERROR_INVALID_STATE;
    delete p; return PS_OK;
}
int32_t PS_CALL ps_player_open_v1(ps_player_handle p, const ps_source_v1* s, ps_request_v1* out) {
    return request(p, out, [&](auto& core) {
        require(s && s->struct_size == sizeof(*s));
        require(s->location && boolean(s->enable_audio) && boolean(s->reconnect));
        require(s->rtsp_transport == PS_RTSP_TCP || s->rtsp_transport == PS_RTSP_UDP);
        require(s->connect_timeout_ms >= 0 && s->read_timeout_ms >= 0 && s->analyze_duration_us >= 0 && s->probe_size > 0);
        require(s->max_reconnect_attempts >= 0 && s->initial_reconnect_delay_ms >= 0 &&
            s->max_reconnect_delay_ms >= s->initial_reconnect_delay_ms && s->stable_playback_reset_seconds >= 0);
        constexpr auto maxMs = std::numeric_limits<int64_t>::max() / 1000000;
        require(s->connect_timeout_ms <= maxMs && s->read_timeout_ms <= maxMs &&
            s->initial_reconnect_delay_ms <= maxMs && s->max_reconnect_delay_ms <= maxMs &&
            s->stable_playback_reset_seconds <= std::numeric_limits<int64_t>::max() / 1000000000);
        require(!s->header_count || s->headers);
        play_core::MediaSource source;
        source.location = s->location; source.enableAudio = s->enable_audio != 0;
        auto& n = source.network;
        n.connectTimeout = std::chrono::milliseconds(s->connect_timeout_ms);
        n.readTimeout = std::chrono::milliseconds(s->read_timeout_ms);
        n.analyzeDuration = std::chrono::microseconds(s->analyze_duration_us); n.probeSize = s->probe_size;
        n.rtspTransport = static_cast<play_core::RtspTransport>(s->rtsp_transport);
        if (s->user_agent) n.userAgent = s->user_agent;
        for (uint32_t i = 0; i < s->header_count; ++i) {
            require(s->headers[i].name && s->headers[i].value);
            n.headers[s->headers[i].name] = s->headers[i].value;
        }
        n.maxReconnectAttempts = s->max_reconnect_attempts;
        n.initialReconnectDelay = std::chrono::milliseconds(s->initial_reconnect_delay_ms);
        n.maxReconnectDelay = std::chrono::milliseconds(s->max_reconnect_delay_ms);
        n.stablePlaybackReset = std::chrono::seconds(s->stable_playback_reset_seconds);
        n.reconnect = s->reconnect != 0;
        return core.open(std::move(source));
    });
}
int32_t PS_CALL ps_player_play_v1(ps_player_handle p, ps_request_v1* out) { return request(p, out, [](auto& c) { return c.play(); }); }
int32_t PS_CALL ps_player_stop_v1(ps_player_handle p, ps_request_v1* out) { return request(p, out, [](auto& c) { return c.stop(); }); }
int32_t PS_CALL ps_player_close_v1(ps_player_handle p, ps_request_v1* out) { return request(p, out, [](auto& c) { return c.close(); }); }
int32_t PS_CALL ps_player_set_paused_v1(ps_player_handle p, int32_t b, ps_request_v1* out) {
    return request(p, out, [&](auto& c) { require(boolean(b)); return c.setPaused(b != 0); });
}
int32_t PS_CALL ps_player_seek_v1(ps_player_handle p, int64_t ms, ps_request_v1* out) {
    return request(p, out, [&](auto& c) { return c.seek(ms); });
}
int32_t PS_CALL ps_player_set_hardware_decode_v1(ps_player_handle p, int32_t h, ps_request_v1* out) {
    return request(p, out, [&](auto& c) { require(hardware(h)); return c.setHardwareDecode(static_cast<play_core::HardwareDecode>(h)); });
}
int32_t PS_CALL ps_player_set_volume_v1(ps_player_handle p, double v, ps_request_v1* out) {
    return request(p, out, [&](auto& c) { return c.setVolume(v); });
}
int32_t PS_CALL ps_player_set_playback_rate_v1(ps_player_handle p, double r, ps_request_v1* out) {
    return request(p, out, [&](auto& c) { return c.setPlaybackRate(r); });
}
int32_t PS_CALL ps_player_select_audio_track_v1(ps_player_handle p, int32_t id, ps_request_v1* out) {
    return request(p, out, [&](auto& c) { return c.selectAudioTrack(id); });
}
int32_t PS_CALL ps_player_select_subtitle_track_v1(ps_player_handle p, int32_t enabled, int32_t id, ps_request_v1* out) {
    return request(p, out, [&](auto& c) { require(boolean(enabled)); return c.selectSubtitleTrack(enabled ? std::optional<int>(id) : std::nullopt); });
}
int32_t PS_CALL ps_event_retain(const ps_event* e, ps_event** out) {
    if (!out) return PS_ERROR_INVALID_ARGUMENT;
    *out = nullptr;
    if (!e || !e->value) return PS_ERROR_INVALID_ARGUMENT;
    try {
        auto copy = std::make_unique<ps_event>(*e->value);
        copy->owned = std::make_unique<play_core::PlayerEvent>(*e->value);
        copy->value = copy->owned.get(); *out = copy.release(); return PS_OK;
    } catch (...) { return PS_ERROR_INTERNAL; }
}
void PS_CALL ps_event_release(ps_event* e) { delete e; }
int32_t PS_CALL ps_event_get_info_v1(const ps_event* e, ps_event_info_v1* out) {
    if (!output(e, out)) return PS_ERROR_INVALID_ARGUMENT;
    const auto& v = *e->value;
    out->type = static_cast<int32_t>(v.type); out->state = static_cast<int32_t>(v.state);
    out->session = v.session; out->position_ms = v.positionMs;
    const auto& op = v.operation;
    out->operation = op.operation; out->operation_session = op.session;
    out->operation_status = static_cast<int32_t>(op.status);
    out->has_requested_position = op.requestedPositionMs.has_value(); out->has_actual_position = op.actualPositionMs.has_value();
    out->requested_position_ms = op.requestedPositionMs.value_or(0); out->actual_position_ms = op.actualPositionMs.value_or(0);
    error(out->error, v.error); error(out->operation_error, op.error); return PS_OK;
}
int32_t PS_CALL ps_event_get_media_info_v1(const ps_event* e, ps_media_info_v1* out) {
    if (!output(e, out)) return PS_ERROR_INVALID_ARGUMENT;
    if (!e->value->mediaInfo) return PS_ERROR_INVALID_STATE;
    const auto& m = *e->value->mediaInfo;
    out->network_source = m.networkSource; out->live = m.live; out->seekable = m.seekable;
    out->has_duration = m.duration.has_value(); out->duration_ms = m.duration ? m.duration->count() : 0;
    out->track_count = static_cast<uint32_t>(m.tracks.size()); out->width = m.width; out->height = m.height;
    out->aspect_num = m.sampleAspectRatio.num; out->aspect_den = m.sampleAspectRatio.den;
    out->rotation_degrees = m.rotationDegrees; return PS_OK;
}
int32_t PS_CALL ps_event_get_track_v1(const ps_event* e, uint32_t i, ps_track_info_v1* out) {
    if (!output(e, out)) return PS_ERROR_INVALID_ARGUMENT;
    if (!e->value->mediaInfo) return PS_ERROR_INVALID_STATE;
    const auto& tracks = e->value->mediaInfo->tracks;
    if (i >= tracks.size()) return PS_ERROR_INVALID_ARGUMENT;
    const auto& t = tracks[i];
    *out = {t.streamIndex, static_cast<int32_t>(t.type), t.language.c_str(), t.title.c_str(), t.codec.c_str(), t.isDefault, t.isForced};
    return PS_OK;
}
int32_t PS_CALL ps_event_get_video_v1(const ps_event* e, ps_video_frame_v1* out) {
    if (!output(e, out)) return PS_ERROR_INVALID_ARGUMENT;
    if (!e->value->video) return PS_ERROR_INVALID_STATE;
    const auto& v = *e->value->video;
    out->format = static_cast<int32_t>(v.format); out->width = v.width; out->height = v.height; out->bytes_per_line = v.bytesPerLine;
    out->aspect_num = v.sampleAspectRatio.num; out->aspect_den = v.sampleAspectRatio.den; out->rotation_degrees = v.rotationDegrees;
    out->color_space = static_cast<int32_t>(v.colorSpace); out->color_range = static_cast<int32_t>(v.colorRange);
    out->unsupported_hdr_transfer = v.unsupportedHdrTransfer; out->has_timestamp = v.timestampMs.has_value(); out->timestamp_ms = v.timestampMs.value_or(0);
    out->bgra = v.bgra ? v.bgra->data() : nullptr; out->bgra_size = v.bgra ? v.bgra->size() : 0;
    for (unsigned i = 0; i < 3; ++i) {
        const auto& p = v.planes[i];
        out->planes[i] = {p.data.get(), p.data && p.stride > 0 && p.height > 0 ? static_cast<uint64_t>(p.stride) * p.height : 0, p.stride, p.height};
    }
    return PS_OK;
}
int32_t PS_CALL ps_event_get_subtitle_v1(const ps_event* e, ps_subtitle_v1* out) {
    if (!output(e, out)) return PS_ERROR_INVALID_ARGUMENT;
    if (!e->value->subtitle) return PS_ERROR_INVALID_STATE;
    const auto& s = *e->value->subtitle;
    *out = {s.startMs, s.endMs, s.assOrText.c_str(), s.assHeader.c_str(), s.assPacket, static_cast<uint32_t>(s.bitmaps.size())};
    return PS_OK;
}
int32_t PS_CALL ps_event_get_subtitle_bitmap_v1(const ps_event* e, uint32_t i, ps_subtitle_bitmap_v1* out) {
    if (!output(e, out)) return PS_ERROR_INVALID_ARGUMENT;
    if (!e->value->subtitle) return PS_ERROR_INVALID_STATE;
    const auto& bitmaps = e->value->subtitle->bitmaps;
    if (i >= bitmaps.size()) return PS_ERROR_INVALID_ARGUMENT;
    const auto& b = bitmaps[i];
    *out = {b.x, b.y, b.width, b.height, b.stride, b.bgra.data(), b.bgra.size()}; return PS_OK;
}
}
