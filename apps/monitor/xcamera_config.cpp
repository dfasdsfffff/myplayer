#include "xcamera_config.h"

#include <cstring>
#include <fstream>
#include <QFile>
#include <QSaveFile>

void XCameraConfig::Push(const XCameraData &data) {
  std::unique_lock<std::mutex> lk(mux_);
  cames_.push_back(data);
}
int XCameraConfig::GetCamCount() {
  std::unique_lock<std::mutex> lk(mux_);
  return cames_.size();
}
XCameraData XCameraConfig::GetCam(int index) {
  std::unique_lock<std::mutex> lk(mux_);
  if (index < 0 || index >= cames_.size()) {
    return XCameraData();
  }
  return cames_[index];
}

bool XCameraConfig::DelCam(int index) {
  std::unique_lock<std::mutex> lk(mux_);
  if (index < 0 || index >= cames_.size()) {
    return false;
  }
  cames_.erase(cames_.begin() + index);
  return true;
}

bool XCameraConfig::SetCam(int index, const XCameraData &data) {
  std::unique_lock<std::mutex> lk(mux_);
  if (index < 0 || index >= cames_.size()) {
    return false;
  }
  cames_[index] = data;
  return true;
}

bool XCameraConfig::Load(const char *path) {
  if (!path) return false;
  QFile file(QString::fromLocal8Bit(path));
  if (!file.open(QIODevice::ReadOnly) || file.size() % sizeof(XCameraData) != 0 ||
      file.size() > static_cast<qint64>(sizeof(XCameraData)) * 10000)
    return false;
  std::vector<XCameraData> cameras;
  while (!file.atEnd()) {
    XCameraData data{};
    if (file.read(reinterpret_cast<char*>(&data), sizeof(data)) != sizeof(data)) return false;
    if (!std::memchr(data.name, 0, sizeof(data.name)) || !std::memchr(data.url, 0, sizeof(data.url)) ||
        !std::memchr(data.sub_url, 0, sizeof(data.sub_url)) || !std::memchr(data.save_path, 0, sizeof(data.save_path)))
      return false;
    cameras.push_back(data);
  }
  std::lock_guard<std::mutex> lock(mux_);
  cames_ = std::move(cameras);
  return true;
}

bool XCameraConfig::Save(const char *path) {
  if (!path) return false;
  QSaveFile file(QString::fromLocal8Bit(path));
  if (!file.open(QIODevice::WriteOnly)) return false;
  std::lock_guard<std::mutex> lock(mux_);
  for (const auto& camera : cames_)
    if (file.write(reinterpret_cast<const char*>(&camera), sizeof(camera)) != sizeof(camera)) return false;
  return file.commit();
}



bool XCameraConfig::Replace(int index, const XCameraData &data) {
  std::unique_lock<std::mutex> lk(mux_);
  if (index < 0 || index >= cames_.size()) {
    return false;
  }
  cames_[index] = data;
  return true;
}

XCameraConfig::XCameraConfig() {}
