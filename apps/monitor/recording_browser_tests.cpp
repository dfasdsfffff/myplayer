#include "xviewer.h"
#include "xcamera_config.h"
#include "xcalendar.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <cstring>
#include <iostream>

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QTemporaryDir temporary;
  if (!temporary.isValid() || !QDir::setCurrent(temporary.path())) return 1;
  int errors = 0;
  auto check = [&](bool ok, const char *message) {
    if (!ok) { std::cerr << message << '\n'; ++errors; }
  };
  XViewer viewer(nullptr, false); // Never load or migrate real user configuration.
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
  // Populate using the same device indices used by the production browser.
  devices->addItem(QStringLiteral("Test camera"));
  devices->addItem(QStringLiteral("Empty camera"));
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
  devices->clear();
  check(clips->count() == 0, "clearing devices clears stale recordings");
  std::cout << "Recording browser errors=" << errors << '\n';
  return errors ? 1 : 0;
}
