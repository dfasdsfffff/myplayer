#pragma once
#include <mutex>
#include <vector>
struct XCameraData {
  char name[1024] = {0};      // 相机名称
  char url[4096] = {0};       // 主码流
  char sub_url[4096] = {0};   // 辅码流
  char save_path[4096] = {0}; // 视频录制保存路径
};

class XCameraConfig {
public:
  static XCameraConfig *Instance() {
    static XCameraConfig ins;
    return &ins;
  }
  // 线程安全
  void Push(const XCameraData &data);
  /// 获取摄像头
  XCameraData GetCam(int index);
  /// 相机数量
  int GetCamCount();
  ///
  bool SetCam(int index, const XCameraData &data);
  ///
  bool DelCam(int index);
  ///
  bool Save(const char *path);
  ///
  bool Load(const char *path);
  ///
  bool Replace(int index, const XCameraData &data);

private:
  XCameraConfig();
  std::vector<XCameraData> cames_;
  std::mutex mux_;
};
