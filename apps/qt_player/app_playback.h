#pragma once
#include <player_sdk_qt/qt_player.h>
#include "enums.h"
#include "media_source.h"
#include "playback_settings.h"
#include <algorithm>
// Application-only presentation/playlist commands. All decoding and lifecycle
// go through QtPlayer's public SDK API; no Core private types are referenced.
class AppPlaybackController {
public:
    player_sdk_qt::QtPlayer player;
    VideoLoopPolicy loopPolicy{LOOP_ALL};
    MediaSource source;
    double volume{.3};
    qint64 position{0};
    AppPlaybackController() {
        QObject::connect(&player,&player_sdk_qt::QtPlayer::positionChanged,&player,[this](qint64 ms) { position = ms; });
    }
    bool play(const MediaSource& s) { source = s; const auto r = player.open(s); if (r) player.play(); return bool(r); }
    bool play(const std::string& s) { return play(MediaSource{s}); }
    void pause() { player.setPaused(player.state() != play_core::PlayerState::Paused && player.state() != play_core::PlayerState::Stopped); }
    void seek(double fraction) { auto info = player.mediaInfo(); if (info && info->duration) player.seek(static_cast<qint64>(std::clamp(fraction,0.,1.) * info->duration->count())); }
    void seekSeconds(int seconds) { player.seek(static_cast<qint64>(seconds)*1000); }
    void seekForward() { player.seek(position+5000); }
    void seekBack() { player.seek(std::max<qint64>(0,position-5000)); }
    void stop() { player.close(); }
    void close() { player.close(); } // RAII joins at destruction; never block the UI on a close request.
    void setVolume(double value) { volume = std::clamp(value,0.,1.); player.setVolume(volume); }
    void setSpeed(double value) { player.setPlaybackRate(value); }
    void setLoopPolicy(VideoLoopPolicy p) { loopPolicy = p; }
    void setHardwareDecodePreference(HardwareDecodePreference p) { player.setHardwareDecode(p); }
    void selectAudioTrack(int id) { audio_ = id; player.selectAudioTrack(id); }
    void selectSubtitleTrack(std::optional<int> id) { subtitle_ = id; player.selectSubtitleTrack(id); }
    void cycleAudioTrack() { cycle(true); }
    void cycleSubtitleTrack() { cycle(false); }
    void addVolume() { setVolume(volume+.1); }
    void subVolume() { setVolume(volume-.1); }
private:
    std::optional<int> audio_, subtitle_;
    void cycle(bool audio) {
        const auto info = player.mediaInfo(); if (!info) return;
        std::vector<int> tracks;
        for (const auto& t : info->tracks) if (t.type == (audio ? play_core::MediaType::Audio : play_core::MediaType::Subtitle)) tracks.push_back(t.streamIndex);
        if (tracks.empty()) return;
        auto& current = audio ? audio_ : subtitle_;
        auto it = current ? std::find(tracks.begin(),tracks.end(),*current) : tracks.end();
        if (it == tracks.end()) current = tracks.front();
        else if (++it != tracks.end()) current = *it;
        else current = audio ? std::optional<int>{tracks.front()} : std::nullopt;
        if (audio) player.selectAudioTrack(*current); else player.selectSubtitleTrack(current);
    }
};
class AppPlaybackSession {
public:
    static std::unique_ptr<AppPlaybackSession> Create() {
        try { return std::make_unique<AppPlaybackSession>(); } catch (const std::exception&) { return {}; }
    }
    AppPlaybackController& controller() { return controller_; }
private:
    AppPlaybackController controller_;
};
