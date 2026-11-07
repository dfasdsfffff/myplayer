#include "xviewer.h"
#include "xcamera_config.h"
#include "xcalendar.h"
#include "xcamera_widget.h"
#include <QMenuBar>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimeEdit>
#include <QSlider>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>
#include <player_sdk_qt/qt_player.h>
#include <play_core/network_runtime.h>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}

namespace {

// A moving, video-only source needs neither an external ffmpeg executable nor
// audio hardware. The same real packets feed both independently bound cameras.
void WriteMediaFixture(const QString &path) {
  const auto bytes = path.toLocal8Bit();
  AVFormatContext *format = nullptr;
  AVCodecContext *codec = nullptr;
  AVFrame *frame = nullptr;
  AVPacket *packet = nullptr;
  auto cleanup = [&] {
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codec);
    if (format && format->pb) avio_closep(&format->pb);
    avformat_free_context(format);
  };
  try {
    const AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_MPEG2VIDEO);
    if (!encoder || avformat_alloc_output_context2(&format, nullptr, "mpegts", bytes.constData()) < 0 || !format)
      throw std::runtime_error("cannot allocate media fixture output");
    AVStream *stream = avformat_new_stream(format, encoder);
    codec = avcodec_alloc_context3(encoder);
    if (!stream || !codec) throw std::runtime_error("cannot allocate media fixture encoder");
    codec->width = 160;
    codec->height = 96;
    codec->pix_fmt = AV_PIX_FMT_YUV420P;
    codec->time_base = AVRational{1, 25};
    codec->framerate = AVRational{25, 1};
    // Every frame carries a sequence header, so a late UDP subscriber can
    // probe immediately without waiting for or decoding a partial GOP.
    codec->gop_size = 1;
    codec->max_b_frames = 0;
    codec->bit_rate = 4000000;
    codec->qmin = 2;
    codec->qmax = 2;
    if (avcodec_open2(codec, encoder, nullptr) < 0 ||
        avcodec_parameters_from_context(stream->codecpar, codec) < 0)
      throw std::runtime_error("cannot open media fixture encoder");
    stream->time_base = codec->time_base;
    if (avio_open(&format->pb, bytes.constData(), AVIO_FLAG_WRITE) < 0 ||
        avformat_write_header(format, nullptr) < 0)
      throw std::runtime_error("cannot write media fixture header");
    frame = av_frame_alloc();
    packet = av_packet_alloc();
    if (!frame || !packet) throw std::runtime_error("cannot allocate media fixture frame");
    frame->format = codec->pix_fmt;
    frame->width = codec->width;
    frame->height = codec->height;
    if (av_frame_get_buffer(frame, 32) < 0)
      throw std::runtime_error("cannot allocate media fixture pixels");
    auto drain = [&] {
      int result = 0;
      while ((result = avcodec_receive_packet(codec, packet)) == 0) {
        av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
        packet->stream_index = stream->index;
        if (av_interleaved_write_frame(format, packet) < 0)
          throw std::runtime_error("cannot write media fixture packet");
        av_packet_unref(packet);
      }
      if (result != AVERROR(EAGAIN) && result != AVERROR_EOF)
        throw std::runtime_error("cannot drain media fixture encoder");
    };
    std::uint32_t noise = 0x12345678;
    for (int index = 0; index < 1125; ++index) {
      if (av_frame_make_writable(frame) < 0)
        throw std::runtime_error("cannot update media fixture frame");
      // Low-entropy color ramps can remain inside the production muxer's I/O
      // buffer for many seconds. Intra-coded deterministic noise gives real
      // video payload ample bandwidth without changing production flushing.
      for (int y = 0; y < frame->height; ++y) {
        for (int x = 0; x < frame->width; ++x) {
          noise ^= noise << 13;
          noise ^= noise >> 17;
          noise ^= noise << 5;
          frame->data[0][y * frame->linesize[0] + x] =
              static_cast<unsigned char>(16 + noise % 220);
        }
      }
      for (int y = 0; y < frame->height / 2; ++y) {
        std::fill_n(frame->data[1] + y * frame->linesize[1], frame->width / 2, 90);
        std::fill_n(frame->data[2] + y * frame->linesize[2], frame->width / 2, 190);
      }
      frame->pts = index;
      if (avcodec_send_frame(codec, frame) < 0)
        throw std::runtime_error("cannot encode media fixture frame");
      drain();
    }
    if (avcodec_send_frame(codec, nullptr) < 0)
      throw std::runtime_error("cannot flush media fixture encoder");
    drain();
    if (av_write_trailer(format) < 0)
      throw std::runtime_error("cannot finalize media fixture");
  } catch (...) {
    cleanup();
    throw;
  }
  cleanup();
}

struct CameraStreams {
  play_core::NetworkRuntime network;
  AVFormatContext *input = nullptr;
  std::array<AVFormatContext *, 2> outputs{};
  std::array<bool, 2> headersWritten{};
  AVPacket *packet = nullptr;
  std::atomic_bool failed{false};
  std::jthread worker;

  bool start(const QString &source, const std::array<QString, 2> &destinations) {
    const auto bytes = source.toLocal8Bit();
    if (avformat_open_input(&input, bytes.constData(), nullptr, nullptr) < 0 ||
        avformat_find_stream_info(input, nullptr) < 0) return false;
    packet = av_packet_alloc();
    if (!packet) return false;
    for (size_t i = 0; i < outputs.size(); ++i) {
      const auto destination = destinations[i].toLatin1();
      auto *&output = outputs[i];
      if (avformat_alloc_output_context2(&output, nullptr, "mpegts", destination.constData()) < 0 || !output)
        return false;
      auto *stream = avformat_new_stream(output, nullptr);
      if (!stream || avcodec_parameters_copy(stream->codecpar, input->streams[0]->codecpar) < 0)
        return false;
      stream->time_base = input->streams[0]->time_base;
      if (avio_open(&output->pb, destination.constData(), AVIO_FLAG_WRITE) < 0 ||
          avformat_write_header(output, nullptr) < 0) return false;
      headersWritten[i] = true;
    }
    worker = std::jthread([this](std::stop_token stop) {
      auto deadline = std::chrono::steady_clock::now();
      while (!stop.stop_requested() && av_read_frame(input, packet) >= 0) {
        for (auto *output : outputs) {
          AVPacket *copy = av_packet_clone(packet);
          if (!copy) { failed = true; return; }
          av_packet_rescale_ts(copy, input->streams[0]->time_base, output->streams[0]->time_base);
          copy->stream_index = 0;
          const int result = av_interleaved_write_frame(output, copy);
          av_packet_free(&copy);
          if (result < 0) { failed = true; return; }
        }
        av_packet_unref(packet);
        deadline += std::chrono::milliseconds(40);
        std::this_thread::sleep_until(deadline);
      }
    });
    return true;
  }

  ~CameraStreams() {
    worker.request_stop();
    if (worker.joinable()) worker.join();
    for (size_t i = 0; i < outputs.size(); ++i) {
      auto *output = outputs[i];
      if (!output) continue;
      if (output->pb) {
        if (headersWritten[i]) av_write_trailer(output);
        avio_closep(&output->pb);
      }
      avformat_free_context(output);
    }
    av_packet_free(&packet);
    avformat_close_input(&input);
  }
};

qint64 RecordingBytes(const QString &path) {
  // Query the live file handle, not cached directory-entry metadata on Windows.
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.size() : -1;
}

bool HasVideoPackets(const QString &path) {
  AVFormatContext *input = nullptr;
  const auto bytes = path.toLocal8Bit();
  if (avformat_open_input(&input, bytes.constData(), nullptr, nullptr) < 0) return false;
  bool found = false;
  AVPacket *packet = av_packet_alloc();
  if (packet && avformat_find_stream_info(input, nullptr) >= 0) {
    const int video = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    while (video >= 0 && av_read_frame(input, packet) >= 0) {
      found = packet->stream_index == video && packet->size > 0;
      av_packet_unref(packet);
      if (found) break;
    }
  }
  av_packet_free(&packet);
  avformat_close_input(&input);
  return found;
}

} // namespace

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  const QString originalDirectory = QDir::currentPath();
  QTemporaryDir temporary;
  struct RestoreDirectory {
    QString path;
    ~RestoreDirectory() {
      if (!QDir::setCurrent(path))
        std::cerr << "Cannot restore original working directory\n";
    }
  } restoreDirectory{originalDirectory};
  if (!temporary.isValid() || !QDir::setCurrent(temporary.path())) return 1;
  int errors = 0;
  auto check = [&](bool ok, const char *message) {
    if (!ok) { std::cerr << message << '\n'; ++errors; }
  };
  XViewer viewer(nullptr, false); // Never load or migrate real user configuration.
  check(viewer.findChildren<QMenuBar *>().isEmpty(), "native menu bar removed");
  check(viewer.OpenPreview({}, 4), "open four-way layout");
  check(viewer.findChildren<XCameraWidget *>().size() == 4, "four camera cards");
  for (int count : {16, 1, 9, 4}) {
    check(viewer.OpenPreview({}, count), "switch layout");
    check(viewer.findChildren<XCameraWidget *>().size() == count, "pane lifetime follows layout");
    auto *button = viewer.findChild<QPushButton *>(QStringLiteral("layout%1").arg(count));
    check(button && button->isChecked(), "layout toolbar selection follows layout");
  }
  check(!viewer.OpenPreview({}, 3), "reject unsupported layout");
  auto *config = XCameraConfig::Instance();
  const QString fixedPath = XCameraConfig::DefaultPath();
  const QString legacy = temporary.filePath(QStringLiteral("legacy/monitor-cams.db"));
  const QString destination = temporary.filePath(QStringLiteral("安装目录/config/monitor-cams.db"));
  XCameraData oldCamera{};
  std::strcpy(oldCamera.name, "Migrated camera");
  std::strcpy(oldCamera.save_path, "relative-recordings");
  config->Push(oldCamera);
  check(config->Save(legacy), "save old configuration fixture");
  check(config->LoadOrMigrate(destination, {legacy}), "migrate to installation config directory");
  check(QFile::exists(legacy) && QFile::exists(destination), "migration preserves original file");
  check(QString::fromLocal8Bit(config->GetCam(0).save_path) ==
        temporary.filePath(QStringLiteral("legacy/relative-recordings")), "preserve relative recording path meaning");
  check(config->LoadOrMigrate(destination, {QStringLiteral("missing")}) && config->GetCamCount() == 1,
        "existing destination takes precedence");
  QDir().mkpath(temporary.filePath(QStringLiteral("different-working-directory")));
  QDir::setCurrent(temporary.filePath(QStringLiteral("different-working-directory")));
  check(XCameraConfig::DefaultPath() == fixedPath, "configuration location independent of working directory");
  QDir::setCurrent(temporary.path());
  const QString corrupt = temporary.filePath(QStringLiteral("corrupt.db"));
  { QFile file(corrupt); check(file.open(QIODevice::WriteOnly), "create invalid config"); file.write("bad"); }
  check(!config->LoadOrMigrate(corrupt, {legacy}), "never overwrite invalid existing destination");
  check(!config->LoadOrMigrate(temporary.filePath(QStringLiteral("not-created.db")), {corrupt}),
        "reject invalid migration source");
  const QString blocked = temporary.filePath(QStringLiteral("blocked"));
  { QFile file(blocked); check(file.open(QIODevice::WriteOnly), "create blocked directory fixture"); }
  check(!config->LoadOrMigrate(blocked + QStringLiteral("/config.db"), {legacy}), "report migration write failure");
  check(config->GetCamCount() == 1, "failed migration preserves memory configuration");
  check(config->LoadOrMigrate(temporary.filePath(QStringLiteral("empty/config.db")), {}), "first launch without configuration");
  check(config->GetCamCount() == 0, "empty initial configuration");
  XCameraData camera{};
  std::strcpy(camera.name, "Test camera");
  const auto root = (temporary.path() + QStringLiteral("/recordings")).toLocal8Bit();
  std::memcpy(camera.save_path, root.constData(), root.size() + 1);
  config->Push(camera);
  std::strcpy(camera.name, "Empty camera");
  config->Push(camera);
  auto *devices = viewer.findChild<QListWidget *>(QStringLiteral("cam_list"));
  auto *clips = viewer.findChild<QListWidget *>(QStringLiteral("time_list"));
  auto *calendar = viewer.findChild<XCalendar *>(QStringLiteral("cal"));
  auto *refresh = viewer.findChild<QPushButton *>(QStringLiteral("refreshRecordings"));
  if (!devices || !clips || !calendar || !refresh) return 1;
  auto *refreshDevices = viewer.findChild<QPushButton *>(QStringLiteral("refreshDevices"));
  if (!refreshDevices) return 1;
  refreshDevices->click();
  check(devices->count() == 2 && devices->item(0)->checkState() == Qt::Unchecked &&
        devices->item(1)->checkState() == Qt::Unchecked, "devices initially unchecked");
  QDir().mkpath(QString::fromLocal8Bit(root) + QStringLiteral("/0"));
  const QDir recordings(QString::fromLocal8Bit(root) + QStringLiteral("/0"));
  auto touch = [&](const QString &name) {
    QFile file(recordings.filePath(name));
    check(file.open(QIODevice::WriteOnly), "create fixture");
  };
  touch(QStringLiteral("cam_2024_01_02_14_05_06.mp4"));
  touch(QStringLiteral("cam_2024_01_02_14_05_06_1.mp4"));
  touch(QStringLiteral("cam_2023_12_31_08_00_00.avi"));
  touch(QStringLiteral("cam_invalid.mp4"));
  touch(QStringLiteral("unrelated.mp4"));
  check(QMetaObject::invokeMethod(&viewer, "Playback", Qt::DirectConnection), "enter playback");
  check(devices->currentRow() == 0, "automatically select first camera");
  check(calendar->selectedDate() == QDate(2024, 1, 2), "select latest available date");
  check(clips->count() == 2, "populate list without calendar click; accept collision suffix");
  if (clips->count())
    check(QFile::exists(clips->item(0)->data(Qt::UserRole).toString()), "retain absolute playback path");
  calendar->setSelectedDate(QDate(2023, 12, 31));
  check(clips->count() == 1, "programmatic/keyboard date selection refreshes list");
  touch(QStringLiteral("cam_2023_12_31_09_00_00.mp4"));
  refresh->click();
  check(clips->count() == 2, "refresh preserves selected available date and discovers new recordings");
  devices->setCurrentRow(1);
  check(clips->count() == 0, "camera change clears previous list");
  auto *empty = viewer.findChild<QLabel *>(QStringLiteral("recordingEmpty"));
  check(empty && !empty->isHidden(), "missing directory displays empty state");
  devices->setCurrentRow(0);
  check(clips->count() == 2, "switching back reloads clips");
  calendar->setSelectedDate(QDate(2022, 1, 1));
  check(clips->count() == 0, "empty date has no stale clips");
  refresh->click();
  check(calendar->selectedDate() == QDate(2024, 1, 2) && clips->count() == 2,
        "refresh falls back from empty date to latest recording date");
  auto *from = viewer.findChild<QTimeEdit *>(QStringLiteral("recordingFrom"));
  auto *to = viewer.findChild<QTimeEdit *>(QStringLiteral("recordingTo"));
  auto *query = viewer.findChild<QPushButton *>(QStringLiteral("queryRecordings"));
  auto *position = viewer.findChild<QSlider *>(QStringLiteral("recordingPosition"));
  check(from && to && query && position, "embedded controls and filters exist");
  if (!from || !to || !query || !position) return 1;
  from->setTime(QTime(15, 0)); query->click();
  check(clips->count() == 0 && !empty->isHidden(), "time filter empty state");
  from->setTime(QTime(14, 5, 6)); to->setTime(QTime(14, 5, 6)); query->click();
  check(clips->count() == 2, "inclusive time filter includes same-second clips");
  from->setTime(QTime(0, 0)); to->setTime(QTime(23, 59, 59)); query->click();
  auto *player = viewer.findChild<player_sdk_qt::QtPlayer *>();
  check(player && !position->isEnabled(), "player starts closed with seek disabled");
  check(QMetaObject::invokeMethod(&viewer, "Preview", Qt::DirectConnection), "return to preview");
  check(calendar->isHidden() || !calendar->isVisibleTo(&viewer), "date filters hidden in preview");
  check(QMetaObject::invokeMethod(&viewer, "Playback", Qt::DirectConnection), "return to playback");
  devices->clear();
  check(clips->count() == 0, "clearing devices clears stale recordings");
  // Exercise real recording tasks using two paced loopback camera streams.
  auto report = [&](bool ok, const char *message) {
    std::cout << (ok ? "PASS: " : "FAIL: ") << message << std::endl;
    if (!ok) ++errors;
  };
  auto waitFor = [&](auto predicate, int milliseconds) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < milliseconds) {
      app.processEvents();
      QThread::msleep(5);
    }
    app.processEvents();
    return predicate();
  };
  const QString media = temporary.filePath(QStringLiteral("moving-camera.ts"));
  try {
    WriteMediaFixture(media);
  } catch (const std::exception &error) {
    std::cerr << "Media fixture: " << error.what() << '\n';
    return 1;
  }
  const int firstPort = 20000 + static_cast<int>(QCoreApplication::applicationPid() % 15000) * 2;
  const std::array<QString, 2> udp{
      QStringLiteral("udp://127.0.0.1:%1?pkt_size=1316").arg(firstPort),
      QStringLiteral("udp://127.0.0.1:%1?pkt_size=1316").arg(firstPort + 1)};
  const QString captureRoot = temporary.filePath(QStringLiteral("selected-recordings"));
  for (int i = 0; i < 2; ++i) {
    auto data = config->GetCam(i);
    const auto url = udp[i].toLatin1();
    const auto path = captureRoot.toLocal8Bit();
    std::memcpy(data.url, url.constData(), url.size() + 1);
    std::memcpy(data.save_path, path.constData(), path.size() + 1);
    config->SetCam(i, data);
  }
  QMetaObject::invokeMethod(&viewer, "Preview", Qt::DirectConnection);
  refreshDevices->click();
  viewer.show();
  app.processEvents();
  auto *startRecording = viewer.findChild<QPushButton *>(QStringLiteral("startSelectedRecording"));
  auto *stopSelected = viewer.findChild<QPushButton *>(QStringLiteral("stopSelectedRecording"));
  auto *stopAll = viewer.findChild<QPushButton *>(QStringLiteral("stopAllRecording"));
  auto *status = viewer.findChild<QLabel *>(QStringLiteral("status"));
  if (!startRecording || !stopSelected || !stopAll || !status) return 1;
  startRecording->click();
  report(status->text().contains(QStringLiteral("先勾选")) && !QDir(captureRoot).exists(),
         "empty recording selection gives a non-blocking hint without creating output");
  report(viewer.OpenPreview({media, media}, 4), "open two visible camera previews");
  auto *firstPreview = viewer.findChild<XCameraWidget *>(QStringLiteral("preview1"));
  auto *secondPreview = viewer.findChild<XCameraWidget *>(QStringLiteral("preview2"));
  auto *closeFirst = viewer.findChild<QPushButton *>(QStringLiteral("closePreview1"));
  if (!firstPreview || !secondPreview || !closeFirst) return 1;
  closeFirst->click(); // Close while the asynchronous open is still in flight.
  report(!firstPreview->player() && !firstPreview->hasFrame(),
         "close during asynchronous opening detaches the player and clears its frame");
  report(firstPreview->open(media, true), "reopen preview after closing during opening");
  report(waitFor([&] { return firstPreview->hasFrame() && secondPreview->hasFrame() &&
                              firstPreview->presentedFrames() > 0 && secondPreview->presentedFrames() > 0; }, 4000),
         "both visible previews present actual decoded media frames");

  CameraStreams streams;
  const bool streaming = streams.start(media, udp);
  report(streaming, "start two independent paced loopback media streams");
  if (!streaming) return 1;
  devices->item(0)->setCheckState(Qt::Checked);
  startRecording->click();
  const QDir firstOutput(captureRoot + QStringLiteral("/0"));
  const QDir secondOutput(captureRoot + QStringLiteral("/1"));
  auto outputs = [](const QDir &dir) {
    return dir.entryInfoList({QStringLiteral("*.mp4")}, QDir::Files, QDir::Name);
  };
  report(waitFor([&] { const auto files = outputs(firstOutput);
                       return !files.isEmpty() && RecordingBytes(files.front().absoluteFilePath()) > 32768; }, 9000),
         "checked camera produces actual recording bytes");
  report(!secondOutput.exists(), "unchecked camera does not create a recording directory");
  report(devices->item(0)->text().contains(QStringLiteral("任务运行中")) &&
         !devices->item(1)->text().contains(QStringLiteral("任务运行中")),
         "per-device UI reports task state without claiming successful recording");
  const auto firstFiles = outputs(firstOutput);
  if (firstFiles.isEmpty()) return 1;
  const QString firstClip = firstFiles.front().absoluteFilePath();
  const qint64 firstBytes = RecordingBytes(firstClip);
  devices->item(1)->setCheckState(Qt::Checked);
  refreshDevices->click();
  report(devices->item(0)->checkState() == Qt::Checked && devices->item(1)->checkState() == Qt::Checked,
         "refresh preserves both recording target checkboxes");
  startRecording->click();
  report(waitFor([&] { const auto files = outputs(secondOutput);
                       return !files.isEmpty() && RecordingBytes(files.front().absoluteFilePath()) > 32768 &&
                              RecordingBytes(firstClip) > firstBytes; }, 9000),
         "adding camera two writes media while camera one continues growing");
  report(outputs(firstOutput).size() == 1 && RecordingBytes(firstClip) > firstBytes,
         "starting the enlarged selection does not restart camera one's existing clip");
  const auto continuingFrames = secondPreview->presentedFrames();
  const qint64 beforeCloseBytes = RecordingBytes(firstClip);
  closeFirst->click();
  report(waitFor([&] { return secondPreview->presentedFrames() > continuingFrames &&
                              RecordingBytes(firstClip) > beforeCloseBytes; }, 2000) &&
         !firstPreview->player() && !firstPreview->hasFrame(),
         "closing one preview keeps the other rendering and its recording task writing");
  report(firstPreview->open(media, true) && waitFor([&] { return firstPreview->hasFrame(); }, 3000),
         "closed preview can reopen and present real frames again");

  devices->setCurrentRow(0);
  QMetaObject::invokeMethod(&viewer, "SetCam", Qt::DirectConnection);
  report(status->text().contains(QStringLiteral("先停止该设备")),
         "editing an active recording device is blocked before opening an editor");
  QMetaObject::invokeMethod(&viewer, "DelCam", Qt::DirectConnection);
  report(config->GetCamCount() == 2 && status->text().contains(QStringLiteral("先停止全部")),
         "deleting devices is blocked while tasks run to prevent index drift");
  devices->item(1)->setCheckState(Qt::Unchecked);
  stopSelected->click();
  const qint64 stoppedBytes = RecordingBytes(firstClip);
  const auto secondFiles = outputs(secondOutput);
  if (secondFiles.isEmpty()) return 1;
  const QString secondClip = secondFiles.front().absoluteFilePath();
  const qint64 secondBytes = RecordingBytes(secondClip);
  report(waitFor([&] { return RecordingBytes(secondClip) > secondBytes; }, 2000) &&
         RecordingBytes(firstClip) == stoppedBytes &&
         !devices->item(0)->text().contains(QStringLiteral("任务运行中")) &&
         devices->item(1)->text().contains(QStringLiteral("任务运行中")),
         "stopping checked camera one finalizes its file while unchecked camera two keeps writing");
  report(HasVideoPackets(firstClip), "stopped camera one's MP4 contains readable video packets");
  stopAll->click();
  report(HasVideoPackets(secondClip) && !streams.failed.load(),
         "stop-all finalizes remaining camera two as readable recorded video");
  const QString blockedCapture = temporary.filePath(QStringLiteral("blocked-capture"));
  { QFile file(blockedCapture); check(file.open(QIODevice::WriteOnly), "create blocked capture root"); }
  auto blockedCamera = config->GetCam(0);
  const auto blockedPath = blockedCapture.toLocal8Bit();
  std::memcpy(blockedCamera.save_path, blockedPath.constData(), blockedPath.size() + 1);
  config->SetCam(0, blockedCamera);
  devices->item(0)->setCheckState(Qt::Checked);
  startRecording->click();
  report(status->text().contains(QStringLiteral("无法创建目录")) &&
         !devices->item(0)->text().contains(QStringLiteral("任务运行中")),
         "uncreatable output directory reports failure and does not register a recording task");
  firstPreview->stop();
  secondPreview->stop();
  std::cout << "Recording browser errors=" << errors << '\n';
  return errors ? 1 : 0;
}
