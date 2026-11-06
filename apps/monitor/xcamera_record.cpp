#include "xcamera_record.h"
#include <play_core/network_runtime.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
}

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <limits>
#include <system_error>

namespace {

namespace fs = std::filesystem;

constexpr auto kReconnectDelay = std::chrono::seconds(3);
constexpr int64_t kNoTimestamp = AV_NOPTS_VALUE;

void LogFfmpegError(const char *operation, int error) {
  char message[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(error, message, sizeof(message));
  av_log(nullptr, AV_LOG_ERROR, "XCameraRecord: %s: %s\n", operation,
         message);
}

void LogMessage(int level, const char *message) {
  av_log(nullptr, level, "XCameraRecord: %s\n", message);
}

std::string MakeOutputPath(const std::string &save_path) {
  std::error_code error;
  const fs::path root = save_path.empty() ? fs::path(".") : fs::path(save_path);
  fs::create_directories(root, error);
  if (error) {
    av_log(nullptr, AV_LOG_ERROR,
           "XCameraRecord: cannot create recording directory: %s\n",
           error.message().c_str());
    return {};
  }

  const std::time_t now = std::time(nullptr);
  std::tm local_time{};
#if defined(_WIN32)
  if (localtime_s(&local_time, &now) != 0)
    return {};
#else
  if (localtime_r(&now, &local_time) == nullptr)
    return {};
#endif

  char name[64] = {};
  if (std::strftime(name, sizeof(name), "cam_%Y_%m_%d_%H_%M_%S.mp4",
                    &local_time) == 0)
    return {};

  // The required name is used normally.  A suffix only avoids overwriting a
  // previous clip when a reconnect/rollover happens in the same second.
  fs::path candidate = root / name;
  for (unsigned int suffix = 1; fs::exists(candidate, error) && !error;
       ++suffix) {
    candidate = root / (std::string(name).substr(
                             0, std::string(name).size() - 4) + "_" +
                         std::to_string(suffix) + ".mp4");
  }
  if (error) {
    av_log(nullptr, AV_LOG_ERROR,
           "XCameraRecord: cannot inspect recording path: %s\n",
           error.message().c_str());
    return {};
  }
  return candidate.string();
}

int64_t PacketTimeUs(const AVPacket *packet, AVRational time_base) {
  const int64_t timestamp = packet->dts != kNoTimestamp ? packet->dts
                                                          : packet->pts;
  if (timestamp == kNoTimestamp || time_base.num == 0 || time_base.den == 0)
    return kNoTimestamp;
  return av_rescale_q(timestamp, time_base, AV_TIME_BASE_Q);
}

int64_t SegmentDurationUs(long long seconds) {
  if (seconds <= 0 ||
      seconds > std::numeric_limits<int64_t>::max() / AV_TIME_BASE)
    return std::numeric_limits<int64_t>::max();
  return static_cast<int64_t>(seconds) * AV_TIME_BASE;
}

struct OutputSegment {
  AVFormatContext *format = nullptr;
  int input_video_index = -1;
  int input_audio_index = -1;
  int output_video_index = -1;
  int output_audio_index = -1;
  int64_t origin_us = kNoTimestamp;
  bool header_written = false;
};

void CloseOutputSegment(OutputSegment &segment) {
  if (!segment.format)
    return;

  if (segment.header_written) {
    const int trailer_result = av_write_trailer(segment.format);
    if (trailer_result < 0)
      LogFfmpegError("write trailer", trailer_result);
  }
  if (!(segment.format->oformat->flags & AVFMT_NOFILE) && segment.format->pb)
    avio_closep(&segment.format->pb);
  avformat_free_context(segment.format);
  segment = {};
}

bool CopyInputStream(AVFormatContext *output, AVStream *input_stream,
                     AVStream **output_stream) {
  *output_stream = avformat_new_stream(output, nullptr);
  if (!*output_stream)
    return false;

  if (avcodec_parameters_copy((*output_stream)->codecpar,
                              input_stream->codecpar) < 0)
    return false;
  (*output_stream)->codecpar->codec_tag = 0;
  (*output_stream)->time_base = input_stream->time_base.num != 0 &&
                                        input_stream->time_base.den != 0
                                    ? input_stream->time_base
                                    : AVRational{1, 90000};
  (*output_stream)->disposition = input_stream->disposition;
  return true;
}

bool CreateOutputSegment(OutputSegment &segment, const std::string &path,
                         AVFormatContext *input, int video_index,
                         int audio_index, int64_t origin_us,
                         bool include_audio) {
  AVFormatContext *output = nullptr;
  int result = avformat_alloc_output_context2(&output, nullptr, "mp4",
                                               path.c_str());
  if (result < 0 || !output) {
    if (result < 0)
      LogFfmpegError("allocate MP4 output", result);
    return false;
  }

  AVStream *video_output = nullptr;
  if (!CopyInputStream(output, input->streams[video_index], &video_output)) {
    LogMessage(AV_LOG_ERROR, "copy video stream parameters");
    avformat_free_context(output);
    return false;
  }

  AVStream *audio_output = nullptr;
  if (include_audio && audio_index >= 0 &&
      !CopyInputStream(output, input->streams[audio_index], &audio_output)) {
    LogMessage(AV_LOG_WARNING, "copy audio stream parameters; video only");
    audio_output = nullptr;
  }

  if (!(output->oformat->flags & AVFMT_NOFILE)) {
    result = avio_open(&output->pb, path.c_str(), AVIO_FLAG_WRITE);
    if (result < 0) {
      LogFfmpegError("open output file", result);
      avformat_free_context(output);
      return false;
    }
  }

  result = avformat_write_header(output, nullptr);
  if (result < 0) {
    LogFfmpegError("write MP4 header", result);
    if (!(output->oformat->flags & AVFMT_NOFILE) && output->pb)
      avio_closep(&output->pb);
    avformat_free_context(output);
    return false;
  }

  segment.format = output;
  segment.input_video_index = video_index;
  segment.input_audio_index = audio_output ? audio_index : -1;
  segment.output_video_index = video_output->index;
  segment.output_audio_index = audio_output ? audio_output->index : -1;
  segment.origin_us = origin_us;
  segment.header_written = true;
  return true;
}

bool OutputSupportsAudio(const AVOutputFormat *format, AVCodecID codec_id) {
  if (!format || codec_id == AV_CODEC_ID_NONE)
    return false;
  // A negative result means the muxer cannot answer.  Try it in that case and
  // let write_header decide; a zero result is an explicit incompatibility.
  return avformat_query_codec(format, codec_id, FF_COMPLIANCE_NORMAL) != 0;
}

bool OpenOutputSegment(OutputSegment &segment, const std::string &save_path,
                       AVFormatContext *input, int video_index, int audio_index,
                       int64_t origin_us) {
  const std::string path = MakeOutputPath(save_path);
  if (path.empty())
    return false;

  AVFormatContext *probe = nullptr;
  int result = avformat_alloc_output_context2(&probe, nullptr, "mp4",
                                               path.c_str());
  if (result < 0 || !probe) {
    if (result < 0)
      LogFfmpegError("probe MP4 output", result);
    return false;
  }
  const bool audio_supported =
      audio_index >= 0 &&
      OutputSupportsAudio(probe->oformat,
                          input->streams[audio_index]->codecpar->codec_id);
  avformat_free_context(probe);

  if (audio_supported &&
      CreateOutputSegment(segment, path, input, video_index, audio_index,
                          origin_us, true))
    return true;

  if (audio_supported)
    LogMessage(AV_LOG_WARNING, "audio could not be muxed; retrying video only");

  // A fresh path is intentional: a failed header may have created a partial
  // file, and a successful retry must never append to it.
  std::error_code error;
  fs::remove(fs::path(path), error);
  const std::string video_only_path = MakeOutputPath(save_path);
  if (video_only_path.empty())
    return false;
  const bool created = CreateOutputSegment(
      segment, video_only_path, input, video_index, -1, origin_us, false);
  if (!created)
    fs::remove(fs::path(video_only_path), error);
  return created;
}

void NormalizePacketTimestamps(AVPacket *packet, AVRational input_time_base,
                               AVRational output_time_base,
                               int64_t origin_us) {
  av_packet_rescale_ts(packet, input_time_base, output_time_base);
  if (origin_us == kNoTimestamp)
    return;

  const int64_t offset =
      av_rescale_q(origin_us, AV_TIME_BASE_Q, output_time_base);
  if (packet->pts != kNoTimestamp)
    packet->pts -= offset;
  if (packet->dts != kNoTimestamp)
    packet->dts -= offset;

  // Packets earlier than the keyframe are discarded by the caller.  This
  // guard also handles small timestamp discontinuities without producing an
  // invalid negative MP4 timeline.
  if (packet->pts != kNoTimestamp && packet->pts < 0)
    packet->pts = 0;
  if (packet->dts != kNoTimestamp && packet->dts < 0)
    packet->dts = 0;
}

} // namespace

XCameraRecord::~XCameraRecord() { Stop(); }

void XCameraRecord::set_rtsp_url(const std::string &url) {
  std::lock_guard<std::mutex> lock(config_mutex_);
  rtsp_url_ = url;
}

void XCameraRecord::set_save_path(const std::string &save_path) {
  std::lock_guard<std::mutex> lock(config_mutex_);
  save_path_ = save_path;
}

void XCameraRecord::set_file_sec(int s) {
  std::lock_guard<std::mutex> lock(config_mutex_);
  file_sec_ = std::max(1, s);
}

void XCameraRecord::Start() {
  std::lock_guard<std::mutex> lock(lifecycle_mutex_);
  if (worker_.joinable())
    return;
  stop_requested_.store(false, std::memory_order_release);
  worker_ = std::thread(&XCameraRecord::Run, this);
}

void XCameraRecord::Stop() {
  {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    stop_requested_.store(true, std::memory_order_release);
  }
  wait_cv_.notify_all();

  std::lock_guard<std::mutex> lock(lifecycle_mutex_);
  if (worker_.joinable())
    worker_.join();
}

int XCameraRecord::InterruptCallback(void *opaque) {
  const auto *recorder = static_cast<const XCameraRecord *>(opaque);
  return recorder &&
                 recorder->stop_requested_.load(std::memory_order_acquire)
             ? 1
             : 0;
}

void XCameraRecord::WaitBeforeReconnect() {
  std::unique_lock<std::mutex> lock(wait_mutex_);
  wait_cv_.wait_for(lock, kReconnectDelay, [this] {
    return stop_requested_.load(std::memory_order_acquire);
  });
}

void XCameraRecord::Run() {
  std::string url;
  std::string save_path;
  long long file_sec = 120;
  {
    std::lock_guard<std::mutex> lock(config_mutex_);
    url = rtsp_url_;
    save_path = this->save_path_;
    file_sec = std::max(1LL, file_sec_);
  }

  if (url.empty()) {
    LogMessage(AV_LOG_ERROR, "RTSP URL is empty");
    return;
  }

  try {
    play_core::NetworkRuntime network;
    while (!stop_requested_.load(std::memory_order_acquire)) {
      RecordSession(url, save_path, file_sec);
      if (!stop_requested_.load(std::memory_order_acquire))
        WaitBeforeReconnect();
    }
  } catch (const std::exception&) {
    LogMessage(AV_LOG_ERROR, "recording worker failed");
  }
}

bool XCameraRecord::RecordSession(const std::string &url,
                                   const std::string &save_path,
                                   long long file_sec) {
  AVFormatContext *input = avformat_alloc_context();
  if (!input) {
    LogMessage(AV_LOG_ERROR, "allocate input context");
    return false;
  }
  input->interrupt_callback.callback = &XCameraRecord::InterruptCallback;
  input->interrupt_callback.opaque = this;

  AVDictionary *options = nullptr;
  // rw_timeout bounds a backend that does not call the interrupt callback
  // frequently; the callback still makes Stop immediate at FFmpeg checkpoints.
  av_dict_set(&options, "rw_timeout", "5000000", 0);
  av_dict_set(&options, "rtsp_transport", "tcp", 0);

  int result = avformat_open_input(&input, url.c_str(), nullptr, &options);
  av_dict_free(&options);
  if (result < 0) {
    if (!stop_requested_.load(std::memory_order_acquire))
      LogFfmpegError("open RTSP input", result);
    avformat_close_input(&input);
    return false;
  }

  result = avformat_find_stream_info(input, nullptr);
  if (result < 0) {
    if (!stop_requested_.load(std::memory_order_acquire))
      LogFfmpegError("find RTSP stream info", result);
    avformat_close_input(&input);
    return false;
  }

  const int video_index =
      av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (video_index < 0) {
    LogMessage(AV_LOG_ERROR, "RTSP input has no video stream");
    avformat_close_input(&input);
    return false;
  }

  const int audio_index =
      av_find_best_stream(input, AVMEDIA_TYPE_AUDIO, -1, video_index, nullptr, 0);

  AVPacket *packet = av_packet_alloc();
  if (!packet) {
    LogMessage(AV_LOG_ERROR, "allocate input packet");
    avformat_close_input(&input);
    return false;
  }

  OutputSegment segment;
  const int64_t split_after_us = SegmentDurationUs(file_sec);
  bool clean_end = false;

  while (!stop_requested_.load(std::memory_order_acquire)) {
    result = av_read_frame(input, packet);
    if (result < 0) {
      clean_end = result == AVERROR_EOF;
      if (!stop_requested_.load(std::memory_order_acquire) && !clean_end)
        LogFfmpegError("read RTSP packet", result);
      av_packet_unref(packet);
      break;
    }

    const bool is_video = packet->stream_index == video_index;
    const bool is_audio = packet->stream_index == audio_index;
    if (!is_video && !is_audio) {
      av_packet_unref(packet);
      continue;
    }

    const AVStream *input_stream = input->streams[packet->stream_index];
    const int64_t packet_time_us = PacketTimeUs(packet, input_stream->time_base);

    // A segment starts only on a video keyframe.  This drops packets from the
    // previous GOP and makes every MP4 independently decodable.
    if (!segment.format) {
      if (!is_video || !(packet->flags & AV_PKT_FLAG_KEY)) {
        av_packet_unref(packet);
        continue;
      }
      const int64_t origin_us = packet_time_us == kNoTimestamp ? 0 : packet_time_us;
      if (!OpenOutputSegment(segment, save_path, input, video_index,
                             audio_index, origin_us)) {
        av_packet_unref(packet);
        break;
      }
    } else if (is_video && (packet->flags & AV_PKT_FLAG_KEY) &&
               packet_time_us != kNoTimestamp &&
               segment.origin_us != kNoTimestamp && split_after_us !=
                   std::numeric_limits<int64_t>::max() &&
               packet_time_us >= segment.origin_us &&
               packet_time_us - segment.origin_us >= split_after_us) {
      // The first keyframe at or after the target closes the old file.  The
      // current packet is then written as the first packet of the new file.
      CloseOutputSegment(segment);
      if (!OpenOutputSegment(segment, save_path, input, video_index,
                             audio_index, packet_time_us)) {
        av_packet_unref(packet);
        break;
      }
    }

    const int output_index = is_video ? segment.output_video_index
                                      : segment.output_audio_index;
    if (output_index < 0) {
      av_packet_unref(packet);
      continue;
    }

    if (packet_time_us != kNoTimestamp && segment.origin_us != kNoTimestamp &&
        packet_time_us < segment.origin_us) {
      av_packet_unref(packet);
      continue;
    }

    AVStream *output_stream = segment.format->streams[output_index];
    NormalizePacketTimestamps(packet, input_stream->time_base,
                               output_stream->time_base, segment.origin_us);
    packet->stream_index = output_index;
    result = av_interleaved_write_frame(segment.format, packet);
    av_packet_unref(packet); // av_read_frame owns this packet until this point.
    if (result < 0) {
      if (!stop_requested_.load(std::memory_order_acquire))
        LogFfmpegError("write MP4 packet", result);
      break;
    }
  }

  av_packet_free(&packet);
  CloseOutputSegment(segment); // writes the trailer when a header was written.
  avformat_close_input(&input);
  return clean_end;
}
