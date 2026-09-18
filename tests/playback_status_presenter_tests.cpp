#include "playback_status_presenter.h"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct TestCase {
    const char* name;
    PlaybackStatus status;
    StatusPresentationKind expectedKind;
    const char* expectedText;
};

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

} // namespace

int main()
{
    using namespace std::chrono_literals;

    const std::vector<TestCase> cases{
        {"opening", {PlaybackState::Opening}, StatusPresentationKind::Transient, "正在打开媒体"},
        {"buffering", {PlaybackState::Buffering}, StatusPresentationKind::Transient, "正在缓冲媒体"},
        {"first reconnect", {PlaybackState::Reconnecting, PlaybackError::ConnectionLost, 1, 5, 2s},
            StatusPresentationKind::Persistent, "正在重连 (1/5)，将在 2 秒后重试"},
        {"last reconnect", {PlaybackState::Reconnecting, PlaybackError::ConnectionLost, 5, 5, 15s},
            StatusPresentationKind::Persistent, "正在重连 (5/5)，将在 15 秒后重试"},
        {"playing", {PlaybackState::Playing}, StatusPresentationKind::Persistent, "正在播放"},
        {"user stopped", {PlaybackState::Stopped}, StatusPresentationKind::Transient, "已停止播放"},
        {"timeout", {PlaybackState::Failed, PlaybackError::Timeout}, StatusPresentationKind::FinalError,
            "播放超时。请检查网络连接后重试。"},
        {"authentication", {PlaybackState::Failed, PlaybackError::Authentication}, StatusPresentationKind::FinalError,
            "身份验证失败。请检查用户名、密码或访问令牌。"},
        {"not found", {PlaybackState::Failed, PlaybackError::NotFound}, StatusPresentationKind::FinalError,
            "找不到媒体。请检查地址或文件是否存在。"},
        {"unsupported protocol", {PlaybackState::Failed, PlaybackError::UnsupportedProtocol}, StatusPresentationKind::FinalError,
            "不支持该协议。请检查媒体地址或协议。"},
        {"invalid media", {PlaybackState::Failed, PlaybackError::InvalidMedia}, StatusPresentationKind::FinalError,
            "媒体无效。请检查文件或地址。"},
        {"decoder failure", {PlaybackState::Failed, PlaybackError::DecoderFailure}, StatusPresentationKind::FinalError,
            "无法解码媒体。请检查编解码器支持。"},
        {"unknown", {PlaybackState::Failed, PlaybackError::Unknown}, StatusPresentationKind::FinalError,
            "播放失败。请检查地址、网络或媒体文件。"},
    };

    for (const TestCase& test : cases) {
        const StatusPresentation presentation = PresentPlaybackStatus(test.status);
        if (!Expect(presentation.kind == test.expectedKind, test.name) ||
            !Expect(presentation.text == QString::fromUtf8(test.expectedText), test.name)) {
            return 1;
        }
    }

    PlaybackStatus sensitive{PlaybackState::Failed, PlaybackError::Authentication};
    sensitive.message = "rtsp://alice:secret@example.test/live?access_token=token-value";
    const QString safeText = PresentPlaybackStatus(sensitive).text;
    if (!Expect(!safeText.contains("alice") && !safeText.contains("secret") &&
                    !safeText.contains("token-value"),
            "presentation must not include media credentials or query secrets")) {
        return 1;
    }

    return 0;
}
