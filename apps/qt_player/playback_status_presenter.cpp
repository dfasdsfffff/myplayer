#include "playback_status_presenter.h"

namespace {

StatusPresentation FinalError(QString text)
{
    return {std::move(text), StatusPresentationKind::FinalError};
}

} // namespace

StatusPresentation PresentPlaybackStatus(const PlaybackStatus& status)
{
    switch (status.state) {
    case PlaybackState::Opening:
        return {QStringLiteral("正在打开媒体"), StatusPresentationKind::Transient};
    case PlaybackState::Buffering:
        return {QStringLiteral("正在缓冲媒体"), StatusPresentationKind::Transient};
    case PlaybackState::Reconnecting:
        return {QStringLiteral("正在重连 (%1/%2)，将在 %3 秒后重试")
                    .arg(status.reconnectAttempt)
                    .arg(status.maxReconnectAttempts)
                    .arg(status.retryAfter.count() / 1000),
            StatusPresentationKind::Persistent};
    case PlaybackState::Playing:
        return {QStringLiteral("正在播放"), StatusPresentationKind::Persistent};
    case PlaybackState::Stopped:
        return {QStringLiteral("已停止播放"), StatusPresentationKind::Transient};
    case PlaybackState::Failed:
        break;
    }

    switch (status.error) {
    case PlaybackError::Timeout:
        return FinalError(QStringLiteral("播放超时。请检查网络连接后重试。"));
    case PlaybackError::Authentication:
        return FinalError(QStringLiteral("身份验证失败。请检查用户名、密码或访问令牌。"));
    case PlaybackError::NotFound:
        return FinalError(QStringLiteral("找不到媒体。请检查地址或文件是否存在。"));
    case PlaybackError::UnsupportedProtocol:
        return FinalError(QStringLiteral("不支持该协议。请检查媒体地址或协议。"));
    case PlaybackError::InvalidMedia:
        return FinalError(QStringLiteral("媒体无效。请检查文件或地址。"));
    case PlaybackError::DecoderFailure:
        return FinalError(QStringLiteral("无法解码媒体。请检查编解码器支持。"));
    case PlaybackError::NetworkUnavailable:
    case PlaybackError::ConnectionLost:
        return FinalError(QStringLiteral("网络连接已中断。请检查网络后重试。"));
    case PlaybackError::Cancelled:
        return {QStringLiteral("已取消播放"), StatusPresentationKind::Transient};
    case PlaybackError::None:
    case PlaybackError::Unknown:
        return FinalError(QStringLiteral("播放失败。请检查地址、网络或媒体文件。"));
    }

    return FinalError(QStringLiteral("播放失败。请检查地址、网络或媒体文件。"));
}
