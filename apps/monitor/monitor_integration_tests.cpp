#define SDL_MAIN_HANDLED
#include "core_video_widget.h"
#include "xcamera_record.h"
#include "media_fixture_builder.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QGridLayout>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <filesystem>
#include <iostream>
#include <atomic>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    const auto root = std::filesystem::path(temporary.path().toStdWString());
    const auto fixtures = BuildMediaFixtures(root, 120);
    const auto source = QString::fromStdWString(fixtures.video.wstring());
    QWidget window;
    auto* grid = new QGridLayout(&window);
    std::vector<CoreVideoWidget*> panes;
    int errors = 0;
    std::atomic_bool audioDisabledRejected{false};
    play_core::Player audioDisabledRuntime;
    auto audioDisabledConnection = audioDisabledRuntime.subscribe([&](const play_core::PlayerEvent& event) {
        if (event.type == play_core::PlayerEvent::Type::Error)
            audioDisabledRejected.store(true, std::memory_order_release);
    });
    play_core::MediaSource silentSource{fixtures.wav.string()};
    silentSource.enableAudio = false;
    if (!audioDisabledRuntime.open(silentSource)) return 1;
    audioDisabledRuntime.play();
    for (int i = 0; i < 16; ++i) {
        auto* pane = new CoreVideoWidget(&window);
        grid->addWidget(pane, i / 4, i % 4);
        QObject::connect(pane, &CoreVideoWidget::playbackError, &app, [&](const QString& message) {
            std::cerr << message.toStdString() << '\n';
            ++errors;
        });
        if (!pane->open(source, true)) return 1;
        panes.push_back(pane);
    }
    window.resize(960, 640);
    window.show();
    XCameraRecord recorder;
    recorder.set_rtsp_url(fixtures.video.string());
    recorder.set_save_path((root / "recordings").string());
    recorder.set_file_sec(2);
    recorder.Start();
    QTimer::singleShot(1500, &app, [&] {
        for (size_t i = 0; i < panes.size(); ++i)
            if (!panes[i]->hasFrame() || panes[i]->presentedFrames() == 0) { std::cerr << "No initial frame in pane " << i << '\n'; ++errors; }
        for (int i = 8; i < 16; ++i) delete panes[i];
        panes.resize(8);
        if (!panes[0]->open(source, true)) { std::cerr << "Source replacement rejected\n"; ++errors; }
        recorder.Stop();
    });
    QTimer::singleShot(3000, &app, [&] {
        if (!audioDisabledRejected.load(std::memory_order_acquire)) { std::cerr << "Audio-disabled source did not report error\n"; ++errors; }
        audioDisabledRuntime.close();
        QElapsedTimer replacementWait;
        replacementWait.start();
        while (!panes[0]->hasFrame() && replacementWait.elapsed() < 2000) {
            app.processEvents();
            QThread::msleep(2);
        }
        for (size_t i = 0; i < panes.size(); ++i)
            if (!panes[i]->hasFrame() || panes[i]->presentedFrames() == 0) { std::cerr << "No final frame in pane " << i << '\n'; ++errors; }
        for (auto* pane : panes) pane->stop();
        int clips = 0;
        for (const auto& entry : std::filesystem::directory_iterator(root / "recordings")) {
            if (entry.path().extension() != ".mp4") continue;
            AVFormatContext* input = nullptr;
            if (avformat_open_input(&input, entry.path().string().c_str(), nullptr, nullptr) < 0) {
                ++errors;
                continue;
            }
            if (avformat_find_stream_info(input, nullptr) < 0 ||
                av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0) < 0)
                ++errors;
            AVPacket* packet = av_packet_alloc();
            if (!packet || av_read_frame(input, packet) < 0) ++errors;
            av_packet_free(&packet);
            avformat_close_input(&input);
            ++clips;
        }
        if (clips < 2) ++errors;
        std::cout << "16-channel preview, source replacement, pane destruction and " << clips
                  << " recorded segments; errors=" << errors << '\n';
        app.exit(errors ? 1 : 0);
    });
    QTimer::singleShot(15000, &app, [&] { app.exit(2); });
    return app.exec();
}
