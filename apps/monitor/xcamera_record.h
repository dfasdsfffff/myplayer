#ifndef XCAMERA_RECORD_H
#define XCAMERA_RECORD_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

// A recorder owns its FFmpeg demux/mux work thread.  It deliberately has no
// Qt or xcodec dependency so that Start/Stop can be used from the GUI thread.
class XCameraRecord {
public:
  XCameraRecord() = default;
  ~XCameraRecord();

  XCameraRecord(const XCameraRecord &) = delete;
  XCameraRecord &operator=(const XCameraRecord &) = delete;

  void set_rtsp_url(const std::string &url);
  void set_save_path(const std::string &save_path);
  void set_file_sec(int s);

  // Start only launches the worker; it never opens the network synchronously.
  void Start();
  // Stop interrupts FFmpeg I/O and joins the worker before returning.
  void Stop();

private:
  void Run();
  bool RecordSession(const std::string &url, const std::string &save_path,
                     long long file_sec);
  void WaitBeforeReconnect();
  static int InterruptCallback(void *opaque);

  std::mutex config_mutex_;
  std::string rtsp_url_;
  std::string save_path_;
  long long file_sec_ = 120;

  std::mutex lifecycle_mutex_;
  std::thread worker_;
  std::atomic<bool> stop_requested_{false};

  std::mutex wait_mutex_;
  std::condition_variable wait_cv_;
};

#endif // XCAMERA_RECORD_H
