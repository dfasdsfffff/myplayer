#include "playback_benchmark.h"

#include "mainwid.h"
#include "app_playback.h"
#include "show.h"
#include "ui_mainwid.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;

struct Measurements {
    std::mutex mutex;
    std::condition_variable completed;
    bool finished = false;
    bool playRequested = false;
    Clock::time_point entry = Clock::now();
    Clock::time_point playStart;
    qint64 delivered = 0;
    qint64 submitted = 0;
    int windowWidth = 0;
    int windowHeight = 0;
    double firstDeliveredMs = -1;
    double firstSubmittedMs = -1;
    double stopRequestedMs = -1;
    double stopFinishedMs = -1;
    QString outcome;
    QString error;

    double sincePlay() const
    {
        return playRequested ? std::chrono::duration<double, std::milli>(Clock::now() - playStart).count() : -1;
    }
};

QJsonValue optionalTime(double milliseconds)
{
    return milliseconds < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(milliseconds);
}

// Caller holds measurements.mutex. The watchdog uses its own file object and
// never touches a GUI QObject or a decoder during forced termination.
bool writeReport(const PlaybackBenchmarkOptions& options, const Measurements& measurements, bool teardownCompleted)
{
    const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - measurements.entry).count();
    QJsonObject report{
        {"schema_version", 3},
        {"benchmark", "myplayer_qt_sdk_playback"},
        {"media", options.mediaPath},
        {"outcome", measurements.outcome},
        {"error", measurements.error},
        {"requested_run_ms", options.runMs},
        {"hard_timeout_ms", options.timeoutMs},
        {"window_width", measurements.windowWidth},
        {"window_height", measurements.windowHeight},
        {"hardware_decode_preference", "disabled"},
        {"playback_speed", 1.0},
        {"gui_delivered_frames", measurements.delivered},
        {"video_frame_sdl_present_returns", measurements.submitted},
        {"first_gui_delivered_frame_ms", optionalTime(measurements.firstDeliveredMs)},
        {"first_video_frame_sdl_present_return_ms", optionalTime(measurements.firstSubmittedMs)},
        {"first_visible_frame_ms", QJsonValue(QJsonValue::Null)},
        {"visibility_measurement", "unavailable: SDL_RenderPresent return does not prove compositor or monitor visibility"},
        {"frame_count_scope", "GUI-delivered video frames and shared VideoWidget SDL_RenderPresent returns; excludes redraws without a fresh frame; not decoded or dropped frames"},
        {"timing_origin", "play request on GUI thread; runner_elapsed_ms starts before MainWid construction"},
        {"stop_requested_ms", optionalTime(measurements.stopRequestedMs)},
        {"stop_finished_ms", optionalTime(measurements.stopFinishedMs)},
        {"stop_latency_ms", optionalTime(measurements.stopRequestedMs >= 0 && measurements.stopFinishedMs >= 0
            ? measurements.stopFinishedMs - measurements.stopRequestedMs : -1)},
        {"runner_elapsed_ms", elapsed},
        {"play_request_to_teardown_ms", optionalTime(teardownCompleted ? measurements.sincePlay() : -1)},
        {"teardown_completed", teardownCompleted}
    };
    QSaveFile file(options.outputPath);
    if (!file.open(QIODevice::WriteOnly)) {
        qCritical() << "Cannot open benchmark report:" << file.errorString();
        return false;
    }
    const QByteArray json = QJsonDocument(report).toJson(QJsonDocument::Indented);
    if (file.write(json) != json.size() || !file.commit()) {
        qCritical() << "Cannot commit benchmark report:" << file.errorString();
        return false;
    }
    return true;
}
}

bool ParsePlaybackBenchmarkOptions(const QStringList& arguments, PlaybackBenchmarkOptions& options, QString& error)
{
    bool benchmarkSeen = false;
    bool mediaSeen = false;
    bool outputSeen = false;
    bool runSeen = false;
    bool timeoutSeen = false;
    for (qsizetype index = 1; index < arguments.size(); ++index) {
        const QString& argument = arguments.at(index);
        if (argument == "--benchmark" && !benchmarkSeen) {
            benchmarkSeen = true;
            continue;
        }
        bool* seen = nullptr;
        if (argument == "--benchmark-media") seen = &mediaSeen;
        else if (argument == "--benchmark-output") seen = &outputSeen;
        else if (argument == "--benchmark-run-ms") seen = &runSeen;
        else if (argument == "--benchmark-timeout-ms") seen = &timeoutSeen;
        if (!seen || *seen || index + 1 >= arguments.size()) {
            error = "Unknown, duplicate, or incomplete benchmark argument: " + argument;
            return false;
        }
        *seen = true;
        const QString& value = arguments.at(++index);
        if (argument == "--benchmark-media") options.mediaPath = value;
        else if (argument == "--benchmark-output") options.outputPath = value;
        else {
            bool valid = false;
            const int milliseconds = value.toInt(&valid);
            if (!valid || milliseconds <= 0 || milliseconds > 3600000) {
                error = argument + " must be an integer in [1, 3600000]";
                return false;
            }
            if (argument == "--benchmark-run-ms") options.runMs = milliseconds;
            else options.timeoutMs = milliseconds;
        }
    }
    if (!benchmarkSeen || !mediaSeen || !outputSeen || options.outputPath.isEmpty() || options.timeoutMs <= options.runMs) {
        error = "Use --benchmark --benchmark-media <local video> --benchmark-output <json> "
                "[--benchmark-run-ms <positive ms>] [--benchmark-timeout-ms <ms greater than run>]";
        return false;
    }
    const QFileInfo media(options.mediaPath);
    if (!media.isFile() || !media.isReadable()) {
        error = "Benchmark media must be a readable local file";
        return false;
    }
    options.mediaPath = media.canonicalFilePath();
    const QFileInfo output(options.outputPath);
    options.outputPath = output.absoluteFilePath();
    if (!output.absoluteDir().exists() || output.isDir() ||
        (!output.canonicalFilePath().isEmpty() && output.canonicalFilePath() == options.mediaPath)) {
        error = "Benchmark output must be a file in an existing directory, distinct from the media";
        return false;
    }
    QSaveFile probe(options.outputPath);
    if (!probe.open(QIODevice::WriteOnly)) {
        error = "Cannot write benchmark output: " + probe.errorString();
        return false;
    }
    probe.cancelWriting();
    return true;
}

int RunPlaybackBenchmark(QApplication& application, const PlaybackBenchmarkOptions& options)
{
    Measurements measurements;
    // Independent of the event loop: bounds GUI stalls, stopAndWait, and teardown.
    std::thread watchdog([&]() {
        std::unique_lock lock(measurements.mutex);
        if (measurements.completed.wait_until(lock, measurements.entry + std::chrono::milliseconds(options.timeoutMs),
                [&]() { return measurements.finished; }))
            return;
        measurements.outcome = "hard_timeout";
        measurements.error = "GUI playback or teardown exceeded the hard deadline";
        writeReport(options, measurements, false);
        std::_Exit(3);
    });

    int exitCode = 1;
    {
        MainWid window;
        if (!window.Init()) {
            std::lock_guard lock(measurements.mutex);
            measurements.outcome = "initialization_failed";
            measurements.error = "MainWid initialization failed";
        } else {
            auto* bridge = window.m_pPlaybackRuntimeBridge;
            auto* controller = window.m_playbackController;
            auto* surface = window.ui->ShowWid;
            // Keep the real bridge/surface path, but observe GUI delivery before
            // the existing surface slot and avoid modal playback-error dialogs.
            QObject::disconnect(bridge, &PlaybackRuntimeBridge::SigVideoFrame, surface, &Show::OnVideoFrame);
            QObject::disconnect(bridge, &PlaybackRuntimeBridge::SigPlaybackStatus, &window, nullptr);
            QObject::connect(bridge, &PlaybackRuntimeBridge::SigVideoFrame, &window, [&](const std::shared_ptr<const VideoFrame>&) {
                std::lock_guard lock(measurements.mutex);
                if (++measurements.delivered == 1)
                    measurements.firstDeliveredMs = measurements.sincePlay();
            });
            QObject::connect(bridge, &PlaybackRuntimeBridge::SigVideoFrame, surface, &Show::OnVideoFrame);
            surface->SetFrameSubmissionMeasurementEnabled(true);
            QObject::connect(surface, &Show::SigVideoFrameSubmitted, &window, [&]() {
                std::lock_guard lock(measurements.mutex);
                if (++measurements.submitted == 1)
                    measurements.firstSubmittedMs = measurements.sincePlay();
            });
            auto requestStop = [&]() {
                {
                    std::lock_guard lock(measurements.mutex);
                    if (measurements.stopRequestedMs >= 0)
                        return;
                    measurements.stopRequestedMs = measurements.sincePlay();
                }
                controller->stop();
            };
            QObject::connect(bridge, &PlaybackRuntimeBridge::SigPlaybackStatus, &window, [&](const PlaybackStatus& status) {
                if (status.state == PlaybackState::Failed) {
                    {
                        std::lock_guard lock(measurements.mutex);
                        measurements.outcome = "playback_failed";
                        measurements.error = QString::fromStdString(status.message);
                    }
                    requestStop();
                    application.exit(1);
                }
            });
            QObject::connect(bridge, &PlaybackRuntimeBridge::SigStopFinished, &window, [&]() {
                std::lock_guard lock(measurements.mutex);
                measurements.stopFinishedMs = measurements.sincePlay();
                if (measurements.outcome.isEmpty()) {
                    if (measurements.submitted == 0) {
                        measurements.outcome = "no_video_submission";
                        measurements.error = "No video frame reached VideoWidget SDL_RenderPresent return";
                    } else {
                        measurements.outcome = measurements.stopRequestedMs >= 0 ? "run_completed" : "end_of_media";
                    }
                }
                application.exit(measurements.error.isEmpty() ? 0 : 1);
            });
            // Reproducible settings; no resume/recent-file writes or playlist
            // insertion: Show::OnPlay uses the same initialized controller.
            controller->setHardwareDecodePreference(HardwareDecodePreference::Disabled);
            controller->setLoopPolicy(LOOP_NONE);
            controller->setSpeed(1.0);
            window.setWindowState(Qt::WindowNoState);
            window.resize(1280, 720);
            window.show();
            QTimer runTimer;
            runTimer.setTimerType(Qt::PreciseTimer);
            runTimer.setSingleShot(true);
            QObject::connect(&runTimer, &QTimer::timeout, &window, requestStop);
            QTimer::singleShot(0, &window, [&]() {
                {
                    std::lock_guard lock(measurements.mutex);
                    measurements.playStart = Clock::now();
                    measurements.playRequested = true;
                    measurements.windowWidth = window.width();
                    measurements.windowHeight = window.height();
                }
                runTimer.start(options.runMs);
                surface->OnPlay(options.mediaPath);
            });
            exitCode = application.exec();
            // Disconnect callbacks before captured locals or the surface die.
            QObject::disconnect(bridge, nullptr, &window, nullptr);
            QObject::disconnect(surface, nullptr, &window, nullptr);
        }
    }
    {
        std::lock_guard lock(measurements.mutex);
        if (measurements.outcome.isEmpty()) {
            measurements.outcome = "interrupted";
            measurements.error = "Application exited before benchmark completion";
            exitCode = 1;
        }
        if (!writeReport(options, measurements, true))
            exitCode = 4;
        measurements.finished = true;
    }
    measurements.completed.notify_one();
    watchdog.join();
    return exitCode;
}
