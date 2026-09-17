#include "reconnect_controller.h"

#include "media_source.h"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

namespace {

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

    ReconnectController controller;
    controller.reset();
    if (!Expect(!controller.cancelled(), "reset clears reconnect cancellation"))
        return 1;
    if (!Expect(controller.wait(0ms), "reset controller permits zero-delay wait"))
        return 1;

    controller.cancel();
    if (!Expect(controller.cancelled(), "cancel marks reconnect cancelled"))
        return 1;
    if (!Expect(!controller.wait(0ms), "cancelled controller rejects wait"))
        return 1;

    controller.reset();
    std::promise<void> entered;
    std::future<void> enteredFuture = entered.get_future();
    std::atomic_bool waitResult{true};

    std::thread waiter([&] {
        entered.set_value();
        waitResult = controller.wait(5s);
    });
    enteredFuture.wait();
    controller.cancel();
    waiter.join();
    if (!Expect(!waitResult.load(), "cancel wakes reconnect wait"))
        return 1;

    MediaSource source{"rtsp://example.test/live"};
    source.network.reconnect = true;
    source.network.maxReconnectAttempts = 3;

    controller.reset();
    if (!Expect(controller.canRetry(source, PlaybackError::Timeout, 0, false),
            "retryable error below attempt limit can retry"))
        return 1;
    if (!Expect(!controller.canRetry(source, PlaybackError::Authentication, 0, false),
            "non-retryable error cannot retry"))
        return 1;
    if (!Expect(!controller.canRetry(source, PlaybackError::Timeout, 3, false),
            "completed attempts at limit cannot retry"))
        return 1;
    if (!Expect(!controller.canRetry(source, PlaybackError::Timeout, 0, true),
            "aborted stream cannot retry"))
        return 1;

    controller.cancel();
    if (!Expect(!controller.canRetry(source, PlaybackError::Timeout, 0, false),
            "cancelled controller cannot retry"))
        return 1;

    controller.reset();
    std::promise<void> gateEntered;
    std::future<void> gateEnteredFuture = gateEntered.get_future();
    std::promise<void> releaseGate;
    std::future<void> releaseGateFuture = releaseGate.get_future();
    std::atomic_bool gateRan{false};
    std::thread gatedWorker([&] {
        const bool ran = controller.runIfNotCancelled([&] {
            gateEntered.set_value();
            releaseGateFuture.wait();
            gateRan.store(true, std::memory_order_release);
        });
        if (!Expect(ran, "reset controller runs gated reconnect handoff"))
            std::terminate();
    });
    gateEnteredFuture.wait();
    std::promise<void> cancelStarted;
    std::future<void> cancelStartedFuture = cancelStarted.get_future();
    std::promise<void> cancelDone;
    std::future<void> cancelDoneFuture = cancelDone.get_future();
    std::thread canceller([&] {
        cancelStarted.set_value();
        controller.cancel();
        cancelDone.set_value();
    });
    cancelStartedFuture.wait();
    if (!Expect(cancelDoneFuture.wait_for(50ms) == std::future_status::timeout,
            "cancel waits for active reconnect handoff"))
        return 1;
    releaseGate.set_value();
    gatedWorker.join();
    canceller.join();
    if (!Expect(gateRan.load(std::memory_order_acquire), "gated reconnect handoff completed"))
        return 1;

    if (!Expect(!controller.runIfNotCancelled([] {}),
            "cancelled controller rejects gated reconnect handoff"))
        return 1;

    MediaSource localSource{"movie.mp4"};
    localSource.network.reconnect = true;
    localSource.network.maxReconnectAttempts = 1;
    controller.reset();
    if (!Expect(controller.canRetry(localSource, PlaybackError::Timeout, 0, false),
            "local file retryability still follows existing error policy"))
        return 1;

    return 0;
}
