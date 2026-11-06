#include "playback_runtime_bridge.h"
#include "app_playback.h"
PlaybackRuntimeBridge::PlaybackRuntimeBridge(QObject* parent) : QObject(parent) {}
void PlaybackRuntimeBridge::attach(AppPlaybackSession* session) {
    if (player_) disconnect(player_,nullptr,this,nullptr);
    player_ = session ? &session->controller().player : nullptr;
    if (!session) return;
    auto& controller = session->controller(); auto* player = &controller.player;
    using QP = player_sdk_qt::QtPlayer;
    connect(player,&QP::videoFrameChanged,this,[this](auto f) { if (f) emit SigFrameDimensionsChanged(f->width,f->height); emit SigVideoFrame(f); });
    connect(player,&QP::subtitleChanged,this,&PlaybackRuntimeBridge::SigSubtitleFrame);
    connect(player,&QP::volumeChanged,this,&PlaybackRuntimeBridge::SigVideoVolume);
    connect(player,&QP::positionChanged,this,[this](qint64 ms) { emit SigVideoPlaySeconds(static_cast<int>(ms/1000)); });
    connect(player,&QP::mediaInfoChanged,this,[this](auto info) {
        if (!info) return;
        emit SigMediaInfo(*info); emit SigVideoTotalSeconds(info->duration ? static_cast<int>(info->duration->count()/1000) : 0);
    });
    connect(player,&QP::stateChanged,this,[this,&controller](auto state) {
        PlaybackStatus status;
        switch(state) {
        case play_core::PlayerState::Opening: status.state = PlaybackState::Opening; emit SigStartPlay(QString::fromUtf8(controller.source.location)); break;
        case play_core::PlayerState::Playing: status.state = PlaybackState::Playing; emit SigPauseStat(false); break;
        case play_core::PlayerState::Paused: emit SigPauseStat(true); return;
        case play_core::PlayerState::Reconnecting: status.state = PlaybackState::Reconnecting; break;
        case play_core::PlayerState::Failed: return; // errorOccurred carries the diagnostic.
        default: return;
        }
        emit SigPlaybackStatus(status);
    });
    connect(player,&QP::closed,this,&PlaybackRuntimeBridge::SigStopFinished);
    connect(player,&QP::errorDetails,this,[this](const play_core::PlayerError& error) {
        PlaybackStatus s; s.state = PlaybackState::Failed; s.error = PlaybackError::Unknown; s.message = error.message;
        using E = play_core::ErrorCode;
        switch(error.code) {
        case E::Timeout: s.error = PlaybackError::Timeout; break;
        case E::Authentication: s.error = PlaybackError::Authentication; break;
        case E::NotFound: s.error = PlaybackError::NotFound; break;
        case E::UnsupportedProtocol: s.error = PlaybackError::UnsupportedProtocol; break;
        case E::NetworkUnavailable: s.error = PlaybackError::NetworkUnavailable; break;
        case E::ConnectionLost: s.error = PlaybackError::ConnectionLost; break;
        case E::InvalidMedia: s.error = PlaybackError::InvalidMedia; break;
        case E::DecoderFailure: s.error = PlaybackError::DecoderFailure; break;
        default: break;
        }
        emit SigPlaybackStatus(s); emit SigPlayMsg(QString::fromUtf8(error.message));
    });
    connect(player,&QP::ended,this,[this,&controller] {
        switch(controller.loopPolicy) {
        case LOOP_ALL: emit SigPlayNextOne(); break;
        case LOOP_RANDOM: emit SigRandomPlayOne(); break;
        case LOOP_SINGLE: controller.play(controller.source); break;
        default: controller.stop(); break;
        }
    });
}
