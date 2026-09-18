#include "latest_video_frame_mailbox.h"
#include "playback_runtime.h"
#include "playback_runtime_bridge.h"

#include <QCoreApplication>
#include <atomic>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace {

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

std::shared_ptr<VideoFrame> NumberedFrame(int number)
{
    auto frame = std::make_shared<VideoFrame>();
    frame->width = number;
    return frame;
}

bool TestNewestFrameReplacesStalledPendingFrame()
{
    LatestVideoFrameMailbox mailbox;
    constexpr int frameCount = 4096;
    std::vector<std::weak_ptr<VideoFrame>> published;
    published.reserve(frameCount);

    std::thread producer([&] {
        for (int number = 1; number <= frameCount; ++number) {
            auto frame = NumberedFrame(number);
            published.push_back(frame);
            mailbox.publish(std::move(frame));
        }
    });
    producer.join();

    auto latest = mailbox.takeLatest();
    if (!Expect(latest && latest->width == frameCount,
            "a stalled consumer receives only the newest published frame"))
        return false;

    for (int number = 0; number < frameCount - 1; ++number) {
        if (!Expect(published[number].expired(),
                "superseded frames are not retained while the UI consumer is stalled"))
            return false;
    }
    return Expect(!published.back().expired(), "the newest pending frame remains available for delivery");
}

bool TestOnlyOneDrainIsScheduledUntilCompletion()
{
    LatestVideoFrameMailbox mailbox;
    if (!Expect(mailbox.markDeliveryScheduled(), "the first pending frame schedules a drain"))
        return false;

    for (int number = 1; number <= 4096; ++number)
        mailbox.publish(NumberedFrame(number));

    if (!Expect(!mailbox.markDeliveryScheduled(),
            "additional frames cannot schedule another drain while one is pending"))
        return false;

    mailbox.deliveryCompleted();
    return Expect(mailbox.markDeliveryScheduled(), "a new drain can be scheduled after the prior drain completes");
}

bool TestBridgeDeliversOnlyNewestFrameAfterWorkerBurst(QCoreApplication& app)
{
    auto runtime = PlaybackRuntime::Create();
    if (!Expect(runtime != nullptr, "runtime should initialize for bridge delivery"))
        return false;

    PlaybackRuntimeBridge bridge;
    bridge.attach(runtime.get());
    int deliveries = 0;
    int deliveredNumber = 0;
    QObject::connect(&bridge, &PlaybackRuntimeBridge::SigVideoFrame, [&] (std::shared_ptr<VideoFrame> frame) {
        ++deliveries;
        deliveredNumber = frame ? frame->width : 0;
    });

    std::thread producer([&] {
        for (int number = 1; number <= 4096; ++number) {
            auto frame = NumberedFrame(number);
            frame->height = 1;
            frame->bytesPerLine = 4;
            frame->bgra.resize(4);
            runtime->SigVideoFrame(std::move(frame));
        }
    });
    producer.join();
    app.processEvents();

    return Expect(deliveries == 1 && deliveredNumber == 4096,
        "one queued bridge drain presents only the newest worker frame");
}

bool TestClearIsSafeWhileProducerIsStopping()
{
    LatestVideoFrameMailbox mailbox;
    std::atomic<bool> stop{false};
    std::thread producer([&] {
        int number = 0;
        while (!stop.load(std::memory_order_relaxed))
            mailbox.publish(NumberedFrame(++number));
    });

    for (int iteration = 0; iteration < 1000; ++iteration)
        mailbox.clear();

    stop.store(true, std::memory_order_relaxed);
    producer.join();
    mailbox.clear();
    return Expect(!mailbox.takeLatest(), "clear removes a pending frame after stop");
}

} // namespace

int main()
{
    int argc = 0;
    QCoreApplication app(argc, nullptr);
    return TestNewestFrameReplacesStalledPendingFrame()
            && TestOnlyOneDrainIsScheduledUntilCompletion()
            && TestBridgeDeliversOnlyNewestFrameAfterWorkerBurst(app)
            && TestClearIsSafeWhileProducerIsStopping()
        ? 0
        : 1;
}
