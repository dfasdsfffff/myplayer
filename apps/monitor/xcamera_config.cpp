#include "xcamera_config.h"

#include <cstring>
#include <fstream>
#include <QFile>
#include <QSaveFile>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

QString XCameraConfig::DefaultPath() {
  return QDir(QCoreApplication::applicationDirPath())
      .filePath(QStringLiteral("config/monitor-cams.db"));
}

bool XCameraConfig::LoadDefault() {
  const QDir appDir(QCoreApplication::applicationDirPath());
  return LoadOrMigrate(DefaultPath(), {
      QDir::current().filePath(QStringLiteral("monitor-cams.db")),
      appDir.filePath(QStringLiteral("monitor-cams.db")),
      appDir.filePath(QStringLiteral("../monitor-cams.db"))});
}

bool XCameraConfig::LoadOrMigrate(const QString &destination, const QStringList &legacyPaths) {
  if (destination.isEmpty()) return false;
  if (QFileInfo::exists(destination)) return Load(destination);
  for (const auto &source : legacyPaths) {
    if (!QFileInfo::exists(source)) continue;
    // Validate in isolation; failed migration must not replace in-memory config.
    XCameraConfig migrated;
    if (!migrated.Load(source)) return false;
    const QDir oldDirectory = QFileInfo(source).absoluteDir();
    for (auto &camera : migrated.cames_) {
      const auto path = QString::fromLocal8Bit(camera.save_path);
      if (path.isEmpty() || !QDir::isRelativePath(path)) continue;
      const auto absolute = QDir::cleanPath(oldDirectory.absoluteFilePath(path)).toLocal8Bit();
      if (absolute.size() >= static_cast<qsizetype>(sizeof(camera.save_path))) return false;
      std::memset(camera.save_path, 0, sizeof(camera.save_path));
      std::memcpy(camera.save_path, absolute.constData(), absolute.size());
    }
    // QSaveFile commits atomically. Keep the source unchanged as a backup.
    if (!migrated.Save(destination)) return false;
    std::lock_guard<std::mutex> lock(mux_);
    cames_ = std::move(migrated.cames_);
    return true;
  }
  std::lock_guard<std::mutex> lock(mux_);
  cames_.clear();
  return true; // A first launch with no saved devices is not an error.
}

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

bool XCameraConfig::Load(const QString &path) {
  if (path.isEmpty()) return false;
  QFile file(path);
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

bool XCameraConfig::Save(const QString &path) {
  if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) return false;
  QSaveFile file(path);
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
