#include "review_compare.h"
#include "media_fixture_builder.h"

#include <player_sdk_qt/qt_player.h>
#include <player_sdk_qt/video_widget.h>

#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QFileInfo>
#include <QPointer>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <vector>

namespace {

bool waitFor(const std::function<bool()>& predicate, int timeout = 8000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout) {
        QApplication::processEvents();
        QThread::msleep(3);
    }
    return predicate();
}

bool expect(bool value, const char* message)
{
    if (!value)
        std::cerr << "FAILED: " << message << '\n';
    return value;
}

std::shared_ptr<const play_core::VideoFrame> frame(std::uint8_t b, std::uint8_t g,
                                                    std::uint8_t r)
{
    auto out = std::make_shared<play_core::VideoFrame>();
    out->width = 8;
    out->height = 8;
    out->bytesPerLine = out->width * 4;
    auto pixels = std::make_shared<std::vector<std::uint8_t>>(
        static_cast<size_t>(out->bytesPerLine * out->height), 0);
    for (size_t i = 0; i < pixels->size(); i += 4) {
        (*pixels)[i] = b;
        (*pixels)[i + 1] = g;
        (*pixels)[i + 2] = r;
        (*pixels)[i + 3] = 255;
    }
    out->bgra = pixels;
    return out;
}

QRgb sample(const QImage& image, int x, int y)
{
    return image.pixel(x * image.width() / 320, y * image.height() / 200);
}

} // namespace

int main(int argc, char** argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("review-compare-tests"));
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 1;

    bool ok = true;
    review::ReviewCompare compare;
    compare.resize(320, 200);
    compare.show();
    QApplication::processEvents();

    // The compare component owns independent VideoWidgets and does not steal
    // the SDK's one-view binding from a host view.
    player_sdk_qt::QtPlayer primary;
    player_sdk_qt::VideoWidget hostView;
    hostView.setPlayer(&primary);
    compare.setPrimary(&primary);
    ok &= expect(hostView.player() == &primary && compare.primaryView()->player() == nullptr,
                 "primary binding remains owned by the host view");

    // Feed deterministic frames directly to exercise all display modes. The
    // composite path is display-domain 8-bit readback by design.
    compare.primaryView()->setVideoFrame(frame(0, 0, 0));
    compare.secondaryView()->setVideoFrame(frame(255, 255, 255));
    compare.setMode(review::ReviewCompare::Mode::Difference);
    QApplication::processEvents();
    QImage difference = compare.grab().toImage();
    ok &= expect(!difference.isNull() && qRed(sample(difference, 160, 100)) > 245
                     && qGreen(sample(difference, 160, 100)) > 245,
                 "difference mode produces an 8-bit absolute difference");

    compare.setMode(review::ReviewCompare::Mode::Wipe);
    compare.setWipePosition(0.25);
    QApplication::processEvents();
    const QImage wipe = compare.grab().toImage();
    ok &= expect(qRed(sample(wipe, 70, 100)) > 245 && qRed(sample(wipe, 280, 100)) < 10,
                 "wipe position exposes B only on the selected side");

    compare.setMode(review::ReviewCompare::Mode::Overlay);
    compare.setOverlayAmount(1.0);
    QApplication::processEvents();
    const QImage overlay = compare.grab().toImage();
    ok &= expect(qRed(sample(overlay, 160, 100)) > 245,
                 "overlay amount is observable in the composed image");

    // A reference frame freezes B while A and the component remain alive.
    compare.setMode(review::ReviewCompare::Mode::Difference);
    compare.captureReference();
    ok &= expect(compare.hasReference(), "reference frame is captured");
    compare.secondaryView()->setVideoFrame(frame(0, 255, 0));
    QApplication::processEvents();
    const QImage frozen = compare.grab().toImage();
    ok &= expect(qRed(sample(frozen, 160, 100)) > 245,
                 "reference frame remains frozen after a later B frame");
    compare.clearReference();
    ok &= expect(!compare.hasReference(), "reference frame can be cleared");

    const auto fixtures = BuildMediaFixtures(
        std::filesystem::path(temporary.path().toStdWString()), 30);
    const QString path = QString::fromStdWString(fixtures.video.wstring());

    // Use real SDK players to exercise serialized B open/pause/seek requests
    // and the A + offset mapping.
    QObject::connect(&compare, &review::ReviewCompare::errorOccurred, &compare, [](const QString& error) {
        std::cerr << "COMPARE: " << error.toStdString() << std::endl;
    });
    compare.setOffset(500);
    ok &= expect(compare.setSecondary(path), "B open request accepted");
    std::vector<play_core::OperationResult> bResults;
    QObject::connect(compare.secondary(), &player_sdk_qt::QtPlayer::operationFinished,
                     &compare, [&](const play_core::OperationResult& result) {
        bResults.push_back(result);
    });
    auto open = primary.open(QUrl::fromLocalFile(path), false);
    ok &= expect(bool(open), "A open request accepted");
    ok &= expect(waitFor([&] { return primary.state() == play_core::PlayerState::Ready
                                      && compare.secondaryDurationMs() > 0; }),
                 "A and B become ready asynchronously");
    auto seek = primary.seek(100);
    ok &= expect(bool(seek), "A seek request accepted");
    ok &= expect(waitFor([&] {
        return std::any_of(bResults.begin(), bResults.end(), [](const auto& result) {
            return result.status == play_core::OperationStatus::Succeeded
                && result.requestedPositionMs && *result.requestedPositionMs == 600;
        });
    }), "B receives A plus offset without polling seeks");

    auto play = primary.play();
    ok &= expect(bool(play), "A play request accepted");
    ok &= expect(waitFor([&] { return compare.primaryPlaying(); }),
                 "comparison tracks A playing state");
    ok &= expect(waitFor([&] { return compare.secondary()->state() == play_core::PlayerState::Playing; }),
                 "B follows A play intent after synchronization");
    primary.setPaused(true);
    ok &= expect(waitFor([&] { return !compare.primaryPlaying()
        && compare.secondary()->state() == play_core::PlayerState::Paused; }), "B follows A pause intent");
    compare.setPlaybackRate(0.5);
    ok &= expect(QFileInfo(compare.secondaryPath()) == QFileInfo(path), "B path is retained");
    ok &= expect(!compare.isFrameLocked(), "comparison advertises non-frame-locked sync");

    // Invalid mapping is represented by state and black B, then recovers.
    compare.setOffset(std::numeric_limits<qint64>::max());
    ok &= expect(compare.secondaryOutOfRange(), "large offset marks B out of range");
    compare.setOffset(0);
    ok &= expect(waitFor([&] { return !compare.secondaryOutOfRange(); }),
                 "valid mapping clears the out-of-range marker");

    compare.setOffset(-200);
    primary.positionChanged(0);
    primary.stateChanged(play_core::PlayerState::Playing);
    ok &= expect(compare.secondaryOutOfRange(), "negative offset begins out of range");
    primary.positionChanged(300);
    ok &= expect(!compare.secondaryOutOfRange(), "playing across negative offset boundary restores B");
    primary.positionChanged(compare.secondaryDurationMs() + 200);
    ok &= expect(compare.secondaryOutOfRange(), "playing across B end marks it out of range");
    primary.stateChanged(play_core::PlayerState::Paused);
    compare.setOffset(0);

    // Replacing B and destroying the owner while requests are in flight must
    // not deliver stale completions into the new player or freed widget.
    ok &= expect(compare.setSecondary(path), "B can be replaced safely");
    auto* transient = new review::ReviewCompare;
    transient->setPrimary(&primary);
    transient->setSecondary(path);
    QPointer<review::ReviewCompare> transientPointer(transient);
    delete transient;
    ok &= expect(transientPointer.isNull(), "comparison destruction is immediate and safe");

    return ok ? 0 : 1;
}
