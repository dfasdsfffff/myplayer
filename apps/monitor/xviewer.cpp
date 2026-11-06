#include "xviewer.h"
#include "ui_XViewer.h"
#include "xcamera_config.h"
#include "xcamera_record.h"
#include "xcamera_widget.h"
#include <player_sdk_qt/qt_player.h>
#include <player_sdk_qt/video_widget.h>
#include <QComboBox>
#include <QSlider>
#include <QTimeEdit>
#include <QSplitter>
#include <QShortcut>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QButtonGroup>
#include <QScreen>
#include <QGuiApplication>
#include <QMessageBox>
#include <QMouseEvent>
#include <QTimerEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QDir>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>
#include <cstring>
#include <iostream>
#include <sstream>
#define C(x) QString::fromLocal8Bit(x)

using namespace std;

void XViewer::contextMenuEvent(QContextMenuEvent *ev) {
  // 鼠标位置显示右键菜单
  left_menu_->exec(QCursor::pos());
  ev->accept();
}
bool XViewer::OpenPreview(const QStringList& sources, int layout) {
  if (layout != 1 && layout != 4 && layout != 9 && layout != 16) return false;
  if (sources.size() > layout) return false;
  view(layout);
  for (qsizetype i = 0; i < sources.size(); ++i)
    if (!cam_wids[i]->open(sources[i], true)) return false;
  return true;
}

void XViewer::view(int count) {
  qDebug() << "View" << count;
  for (auto *button : ui->head->findChildren<QPushButton *>()) {
    if (button->property("layoutCount").isValid())
      button->setChecked(button->property("layoutCount").toInt() == count);
  }
  int cols = sqrt(count);
  int rows = cols;
  int wid_seze = static_cast<int>(cam_wids.size());
  //
  QGridLayout *lay = nullptr;
  lay = static_cast<QGridLayout *>(ui->cams->layout());
  if (!lay) {
    lay = new QGridLayout(ui->cams);
  }
  lay->setContentsMargins(2, 2, 2, 2);
  lay->setHorizontalSpacing(2);
  lay->setVerticalSpacing(2);
  for (int row = 0; row < 4; ++row)
    lay->setRowStretch(row, row < rows ? 1 : 0);
  for (int col = 0; col < 4; ++col)
    lay->setColumnStretch(col, col < cols ? 1 : 0);
  //
  int i = 0;
  for (; i < count; ++i) {
    if (!cam_wids[i]) {
      cam_wids[i] = new XCameraWidget(this);
      cam_wids[i]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
      cam_wids[i]->setStyleSheet("background-color:rgb(38,43,49)");
    }
    lay->addWidget(cam_wids[i], i / rows, i % cols);
  }
  //
  for (; i < wid_seze; ++i) {
    if (cam_wids[i]) {
      delete cam_wids[i];
      cam_wids[i] = nullptr;
    }
  }
}

void XViewer::RefreshCams() {
  auto c = XCameraConfig::Instance();
  ui->cam_list->clear();
  int count = c->GetCamCount();
  for (int i = 0; i < count; ++i) {
    auto cam = c->GetCam(i);
    auto item =
        new QListWidgetItem(QIcon(":/img/cam.png"), C(cam.name), ui->cam_list);
    item->setData(Qt::UserRole, i);
    ui->cam_list->addItem(item);
  }
}

void XViewer::SetCam(int index) {
  auto c = XCameraConfig::Instance();
  QDialog dlg(this);
  dlg.resize(800, 200);
  QFormLayout *lay = new QFormLayout(&dlg);
  QLineEdit *name_edit = new QLineEdit(&dlg);
  lay->addRow("名称", name_edit);
  QLineEdit *url_edit = new QLineEdit(&dlg);
  lay->addRow("主码流", url_edit);
  QLineEdit *suburl_edit = new QLineEdit(&dlg);
  lay->addRow("辅码流", suburl_edit);
  QLineEdit *save_path_edit = new QLineEdit(&dlg);
  lay->addRow("保存地址", save_path_edit);
  QPushButton *save_button = new QPushButton("保存", &dlg);
  lay->addRow("", save_button);
  QObject::connect(save_button, &QPushButton::clicked, &dlg, &QDialog::accept);
  if (index >= 0 && index < ui->cam_list->count()) { // 编辑 读入元数据显示
    auto cam = c->GetCam(index);
    name_edit->setText(C(cam.name));
    url_edit->setText(C(cam.url));
    suburl_edit->setText(C(cam.sub_url));
    save_path_edit->setText(C(cam.save_path));
  } else if (index == -1) { // 添加
  }
  auto f = [this, lay](QWidget *field) {
    QWidget *labelWidget = lay->labelForField(field);
    auto label = qobject_cast<QLabel *>(labelWidget);
    QMessageBox box;
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle("提示");
    box.setText(QString("请输入%1").arg(label->text()));
    box.addButton(QMessageBox::Ok)->setText("确认");
    box.exec();
  };
  for (;;) {
    if (dlg.exec() == QDialog::Accepted) {
      if (name_edit->text().isEmpty()) {
        f(name_edit);
        continue;
      }
      if (url_edit->text().isEmpty()) {
        f(url_edit);
        continue;
      }
      if (suburl_edit->text().isEmpty()) {
        f(suburl_edit);
        continue;
      }
      if (save_path_edit->text().isEmpty()) {
        f(save_path_edit);
        continue;
      }
      break;
    }
    return;
  }
  XCameraData data{};
  const auto copyField = [&dlg](auto& destination, const QString& text) {
    const auto bytes = text.toLocal8Bit();
    if (bytes.size() >= static_cast<qsizetype>(sizeof(destination))) {
      QMessageBox::warning(&dlg, "提示", "输入内容过长，配置未保存。");
      return false;
    }
    std::memcpy(destination, bytes.constData(), static_cast<size_t>(bytes.size()) + 1);
    return true;
  };
  if (!copyField(data.name, name_edit->text()) || !copyField(data.url, url_edit->text()) ||
      !copyField(data.sub_url, suburl_edit->text()) ||
      !copyField(data.save_path, QDir(save_path_edit->text()).absolutePath()))
    return;
  if (index == -1) // 添加
    c->Push(data);
  else { // 修改
    c->SetCam(index, data);
  }
  if (!c->Save())
    QMessageBox::warning(this, QStringLiteral("配置保存失败"),
        QStringLiteral("无法保存设备配置，请检查目录写入权限：\n%1\n本次修改仅在当前进程中生效。")
            .arg(XCameraConfig::DefaultPath()));
  RefreshCams();
}

XViewer::XViewer(QWidget *parent, bool loadConfiguration) : QWidget{parent} {
  ui = new Ui::XViewer();
  ui->setupUi(this);
  setWindowFlags(Qt::Window);
  setWindowTitle(QStringLiteral("MyPlayer 多路监控"));
  setWindowIcon(QIcon(QStringLiteral(":/img/xv.ico")));
  const QSize available = QGuiApplication::primaryScreen()->availableGeometry().size();
  resize(qMin(1200, available.width() - 60), qMin(760, available.height() - 80));
  setMinimumSize(980, 680);
  setStyleSheet(R"(
    QWidget#XViewer, QWidget#body, QWidget#cams { background: #171a1f; color: #e6e9ed; font-family: "Microsoft YaHei UI"; font-size: 11px; }
    QWidget#head { background: #202328; }
    QWidget#left { background: #202328; }
    QPushButton { color: #dce1e6; background: #2b3036; border: 1px solid #3a4149; border-radius: 3px; padding: 2px 6px; font-size: 11px; }
    QPushButton:hover { background: #353c44; }
    QPushButton:checked { background: #24573f; border-color: #327854; color: #e8fff2; }
    QListWidget, QCalendarWidget QWidget { background: #1b1f24; color: #dce1e6; border: 1px solid #343a42; font-size: 11px; }
    QListWidget::item { padding: 3px 3px; }
    QListWidget::item:selected { background: #284b3b; }
    QMenuBar { background: #25282d; color: #e6e9ed; font-size: 11px; }
    QMenuBar::item { padding: 3px 7px; background: transparent; }
    QMenuBar::item:selected, QMenu::item:selected { background: #343b43; }
    QMenu { background: #25282d; color: #e6e9ed; border: 1px solid #3a4149; font-size: 11px; }
    QLabel { color: #c7cdd3; font-size: 11px; }
  )");
  QVBoxLayout *thisVLayout = new QVBoxLayout(this);
  thisVLayout->setContentsMargins(0, 0, 0, 0);
  thisVLayout->setSpacing(0);

  auto *menuBar = new QMenuBar(this);
  auto *fileMenu = menuBar->addMenu(QStringLiteral("文件(&F)"));
  fileMenu->addAction(QStringLiteral("添加摄像机..."), this, &XViewer::AddCam);
  fileMenu->addSeparator();
  fileMenu->addAction(QStringLiteral("退出"), this, &QWidget::close);
  auto *viewMenu = menuBar->addMenu(QStringLiteral("视图(&V)"));
  viewMenu->addAction(QStringLiteral("1 分屏"), this, &XViewer::view1);
  viewMenu->addAction(QStringLiteral("4 分屏"), this, &XViewer::view4);
  viewMenu->addAction(QStringLiteral("9 分屏"), this, &XViewer::view9);
  viewMenu->addAction(QStringLiteral("16 分屏"), this, &XViewer::view16);
  auto *recordMenu = menuBar->addMenu(QStringLiteral("录像(&R)"));
  recordMenu->addAction(QStringLiteral("开始全部录像"), this, &XViewer::StartRecord);
  recordMenu->addAction(QStringLiteral("停止全部录像"), this, &XViewer::StopRecord);
  menuBar->addMenu(QStringLiteral("帮助(&H)"));
  thisVLayout->addWidget(menuBar);
  thisVLayout->addWidget(ui->head);
  thisVLayout->addWidget(ui->body);

  ui->logo->hide();
  ui->min->hide();
  ui->max->hide();
  ui->normal->hide();
  ui->close->hide();
  ui->head->setMaximumHeight(38);
  ui->left->setFixedWidth(248);
  ui->cam_list->setIconSize(QSize(18, 18));
  ui->add_cam->setText(QStringLiteral("添加"));
  ui->set_cam->setText(QStringLiteral("编辑"));
  ui->del_cam->setText(QStringLiteral("删除"));
  ui->status->setText(QStringLiteral("● 监控中"));
  ui->status->setStyleSheet(QStringLiteral("color:#35c88a; padding-right:12px;"));
  ui->preview->setText(QStringLiteral("实时预览"));
  ui->playback->setText(QStringLiteral("录像回放"));

  QHBoxLayout *headHLayout = new QHBoxLayout(ui->head);
  headHLayout->addWidget(ui->preview);
  ui->preview->setMinimumSize(56, 20);
  ui->playback->setMinimumSize(56, 20);
  headHLayout->addWidget(ui->playback);
  auto *layoutButtons = new QButtonGroup(this);
  for (int count : {1, 4, 9, 16}) {
    auto *button = new QPushButton(QStringLiteral("%1 分屏").arg(count), ui->head);
    button->setCheckable(true);
    button->setChecked(count == 9);
    button->setProperty("layoutCount", count);
    layoutButtons->addButton(button);
    headHLayout->addWidget(button);
    connect(button, &QPushButton::clicked, this, [this, count] { view(count); });
  }
  auto *recordButton = new QPushButton(QStringLiteral("全部录像"), ui->head);
  headHLayout->addWidget(recordButton);
  connect(recordButton, &QPushButton::clicked, this, &XViewer::StartRecord);
  auto *stopButton = new QPushButton(QStringLiteral("停止录像"), ui->head);
  headHLayout->addWidget(stopButton);
  connect(stopButton, &QPushButton::clicked, this, &XViewer::StopRecord);
  headHLayout->addStretch(1);
  headHLayout->addWidget(ui->status);
  headHLayout->setContentsMargins(12, 4, 10, 4);
  headHLayout->setSpacing(6);

  auto *sideLayout = qobject_cast<QVBoxLayout *>(ui->left->layout());
  sideLayout->setSpacing(8);
  sideLayout->setAlignment(Qt::AlignTop);
  auto *deviceTitle = new QLabel(QStringLiteral("设备列表"), ui->left);
  sideLayout->insertWidget(0, deviceTitle);
  auto *search = new QLineEdit(ui->left);
  search->setPlaceholderText(QStringLiteral("搜索设备"));
  search->setStyleSheet(QStringLiteral("background:#2b3036;color:#dce1e6;border:1px solid #3a4149;border-radius:2px;padding:4px;font-size:11px;"));
  sideLayout->insertWidget(1, search);
  connect(search, &QLineEdit::textChanged, this, [this](const QString &text) {
    for (int i = 0; i < ui->cam_list->count(); ++i) {
      auto *item = ui->cam_list->item(i);
      item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
    }
  });
  ui->add_cam->setMinimumWidth(0);
  ui->set_cam->setMinimumWidth(0);
  ui->del_cam->setMinimumWidth(0);
  ui->horizontalLayout->setSpacing(3);
  auto *hint = new QLabel(QStringLiteral("拖拽设备到画面开始预览  ·  右键切换布局"), this);
  hint->setContentsMargins(10, 3, 10, 3);
  thisVLayout->addWidget(hint);

  QHBoxLayout *bodyHLayout = new QHBoxLayout(ui->body);
  bodyHLayout->setContentsMargins(8, 8, 8, 8);
  bodyHLayout->setSpacing(8);
  bodyHLayout->addWidget(ui->left);
  bodyHLayout->addWidget(ui->cams, 1);
  bodyHLayout->addWidget(ui->playback_wid, 1);
  connect(ui->normal, &QPushButton::clicked, this, &XViewer::NormalWindow);
  connect(ui->max, &QPushButton::clicked, this, &XViewer::MaxWindow);
  ui->normal->hide();
  ui->max->hide();

  left_menu_ = new QMenu(this);
  auto *contextViewMenu = left_menu_->addMenu("视图");
  auto a = contextViewMenu->addAction("1窗口");
  connect(a, &QAction::triggered, this, &XViewer::view1);
  a = contextViewMenu->addAction("4窗口");
  connect(a, &QAction::triggered, this, &XViewer::view4);
  a = contextViewMenu->addAction("9窗口");
  connect(a, &QAction::triggered, this, &XViewer::view9);
  a = contextViewMenu->addAction("16窗口");
  connect(a, &QAction::triggered, this, &XViewer::view16);
  a = left_menu_->addAction("全部开始录制");
  connect(a, &QAction::triggered, this, &XViewer::StartRecord);
  a = left_menu_->addAction("全部停止录制");
  connect(a, &QAction::triggered, this, &XViewer::StopRecord);
  // 默认9个视图
  view(9);
  if (loadConfiguration && !XCameraConfig::Instance()->LoadDefault()) {
    QTimer::singleShot(0, this, [this] {
      QMessageBox::warning(this, QStringLiteral("配置加载失败"),
          QStringLiteral("无法加载或迁移设备配置。请检查文件格式和目录权限：\n%1\n原文件未删除，也不会覆盖已有配置。")
              .arg(XCameraConfig::DefaultPath()));
    });
  }
  ui->time_list->clear();

  SetupPlaybackUi();
  RefreshCams();
  //
  connect(ui->add_cam, &QPushButton::clicked, this, &XViewer::AddCam);
  connect(ui->set_cam, SIGNAL(clicked()), this, SLOT(SetCam()));
  connect(ui->del_cam, &QPushButton::clicked, this, &XViewer::DelCam);
  connect(ui->preview, &QPushButton::clicked, this, &XViewer::Preview);
  connect(ui->playback, &QPushButton::clicked, this, &XViewer::Playback);
  Preview();
  connect(ui->cam_list, &QListWidget::currentRowChanged, this,
          [this](int) { RefreshRecordings(); });
  connect(ui->cal, &XCalendar::selectionChanged, this,
          [this] { SelectDate(ui->cal->selectedDate()); });
  connect(ui->time_list, &QListWidget::doubleClicked, this,
          &XViewer::PlayVideo);
  //
}

XViewer::~XViewer() {
  ResetPlayback();
  StopRecord();
  for (auto*& pane : cam_wids) {
    delete pane;
    pane = nullptr;
  }
  delete ui;
}
void XViewer::MaxWindow() {
  ui->normal->show();
  ui->max->hide();
  this->showMaximized();
}

void XViewer::NormalWindow() {
  ui->normal->hide();
  ui->max->show();
  this->showNormal();
}
void XViewer::view1() { view(1); }

void XViewer::view4() { view(4); }

void XViewer::view9() { view(9); }

void XViewer::view16() { view(16); }

void XViewer::AddCam() { SetCam(-1); }

void XViewer::SetCam() {
  int row = ui->cam_list->currentRow();
  if (row < 0) {
    QMessageBox::information(this, "提示", "前选择摄像机");
    return;
  }
  SetCam(row);
}

void XViewer::DelCam() {
  int row = ui->cam_list->currentRow();
  if (row < 0) {
    QMessageBox::information(this, "提示", "前选择摄像机");
    return;
  }
  std::stringstream ss;
  ss << "您确认要删除摄像机" << ui->cam_list->item(row)->text().toStdString()
     << "吗？";
  QString s = QString::fromStdString(ss.str());
  auto button = QMessageBox::question(this, "询问", s);
  if (button == QMessageBox::Yes) {
    auto c = XCameraConfig::Instance();
    c->DelCam(row);
    if (!c->Save())
      QMessageBox::warning(this, QStringLiteral("配置保存失败"),
          QStringLiteral("无法保存设备配置，请检查目录写入权限：\n%1\n本次修改仅在当前进程中生效。")
              .arg(XCameraConfig::DefaultPath()));
    RefreshCams();
  }
}

void XViewer::StartRecord() {
  StopRecord();
  ui->status->setText("正在录制...");
  // 获取录制列表
  auto conf = XCameraConfig::Instance();
  int count = conf->GetCamCount();
  QDir dir;
  for (int i = 0; i < count; i++) {
    auto cam = conf->GetCam(i);
    stringstream ss;
    ss << cam.save_path << "/" << i << "/";
    dir.mkpath(QString::fromStdString(ss.str()));
    XCameraRecord *record = new XCameraRecord();
    record->set_rtsp_url(cam.url);
    record->set_save_path(ss.str());
    record->set_file_sec(120);
    record->Start();
    records.push_back(record);
  }
  // 创建录制目录
  // 分别开始录制线程
}

void XViewer::StopRecord() {
  ui->status->setText("已停止录制");
  for (auto record : records) {
    record->Stop();
    delete record;
  }
  records.clear();
  if (recording_summary_ && ui->playback_wid->isVisible())
    RefreshRecordings();
}

void XViewer::Preview() {
  ResetPlayback();
  date_filters_->hide();
  ui->cam_list->setMaximumHeight(QWIDGETSIZE_MAX);
  ui->cams->show();
  ui->playback_wid->hide();
}

void XViewer::Playback() {
  date_filters_->show();
  ui->cam_list->setMaximumHeight(100);
  ui->playback_wid->show();
  ui->cams->hide();
  if (ui->cam_list->currentRow() < 0 && ui->cam_list->count() > 0)
    ui->cam_list->setCurrentRow(0);
  RefreshRecordings();
}

void XViewer::RefreshRecordings() {
  SelectCamera(ui->cam_list->currentIndex());
  scan_status_->setText(QStringLiteral("最近扫描 %1").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))));
}

void XViewer::SelectCamera(const QModelIndex &index) {
  ResetPlayback();
  const auto selectedDate = ui->cal->selectedDate();
  ui->cal->ClearDate();
  ui->time_list->clear();
  cam_videos.clear();
  recording_path_->clear();
  if (!index.isValid()) {
    SelectDate(selectedDate);
    recording_empty_->setText(QStringLiteral("请选择设备以查看录像。"));
    return;
  }
  const auto data = XCameraConfig::Instance()->GetCam(index.row());
  const QDir root(QString::fromLocal8Bit(data.save_path));
  const QDir dir(root.filePath(QString::number(index.row())));
  recording_path_->setText(QStringLiteral("录像目录：%1").arg(QDir::toNativeSeparators(dir.absolutePath())));
  // Match recorder names, including same-second collision suffixes.
  static const QRegularExpression namePattern(
      QStringLiteral("^cam_(\\d{4}_\\d{2}_\\d{2}_\\d{2}_\\d{2}_\\d{2})(?:_\\d+)?$"));
  const auto files = dir.entryInfoList({QStringLiteral("*.mp4"), QStringLiteral("*.avi")},
                                     QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
  for (const auto &file : files) {
    const auto match = namePattern.match(file.completeBaseName());
    if (!match.hasMatch())
      continue;
    const auto dateTime = QDateTime::fromString(match.captured(1), QStringLiteral("yyyy_MM_dd_HH_mm_ss"));
    if (!dateTime.isValid())
      continue;
    ui->cal->AddDate(dateTime.date());
    cam_videos[dateTime.date()].push_back({file.absoluteFilePath(), dateTime});
  }
  for (auto &[date, videos] : cam_videos) {
    std::sort(videos.begin(), videos.end(), [](const XCamVideo &a, const XCamVideo &b) {
      return a.datetime == b.datetime ? a.filepath < b.filepath : a.datetime < b.datetime;
    });
  }
  const auto date = cam_videos.empty() || cam_videos.contains(selectedDate)
                        ? selectedDate : cam_videos.rbegin()->first;
  {
    const QSignalBlocker blocker(ui->cal);
    ui->cal->setSelectedDate(date);
    ui->cal->setCurrentPage(date.year(), date.month());
  }
  SelectDate(date);
  if (!dir.exists() || !dir.isReadable())
    recording_empty_->setText(QStringLiteral("录像目录不存在或不可读，请检查保存地址及访问权限。"));
}

void XViewer::SelectDate(QDate date) {
  ResetPlayback();
  ui->time_list->clear();
  const auto found = cam_videos.find(date);
  if (found != cam_videos.end()) {
    for (const auto &video : found->second) {
      if (video.datetime.time() < filter_from_->time() || video.datetime.time() > filter_to_->time()) continue;
      auto *item = new QListWidgetItem(
          video.datetime.time().toString(QStringLiteral("HH:mm:ss")) +
          QString(QChar(10)) + QFileInfo(video.filepath).fileName());
      item->setSizeHint(QSize(240, 68));
      item->setData(Qt::UserRole, video.filepath);
      item->setToolTip(QDir::toNativeSeparators(video.filepath));
      ui->time_list->addItem(item);
    }
  }
  recording_summary_->setText(QStringLiteral("%1 · %2 段录像")
      .arg(date.toString(QStringLiteral("yyyy-MM-dd"))).arg(ui->time_list->count()));
  recording_empty_->setText(cam_videos.empty()
      ? QStringLiteral("该设备暂无录像。请确认保存目录，录制完成后点击刷新。")
      : QStringLiteral("所选日期或开始时间范围内没有录像。请调整筛选条件。"));
  recording_empty_->setVisible(ui->time_list->count() == 0);

}

void XViewer::PlayVideo(const QModelIndex &index) {
  auto *item = ui->time_list->item(index.row());
  if (!item) return;
  ResetPlayback();
  playback_path_ = item->data(Qt::UserRole).toString();
  playback_file_->setText(QFileInfo(playback_path_).fileName());
  playback_file_->setToolTip(playback_path_);
  const auto request = playback_player_->open(QUrl::fromLocalFile(playback_path_));
  if (request) playback_player_->play();
  else playback_file_->setText(QStringLiteral("无法受理播放请求，请重试。"));
}

void XViewer::ResetPlayback() {
  if (!playback_player_) return;
  playback_player_->close();
  playback_path_.clear();
  playback_duration_ = 0;
  playback_view_->setVideoFrame({});
  playback_position_->setValue(0);
  playback_position_->setEnabled(false);
  playback_pause_->setEnabled(false);
  playback_clock_->setText(QStringLiteral("00:00 / --:--"));
  playback_file_->setText(QStringLiteral("双击右侧录像开始回放"));
}

////////////////////////////////////////
/// 鼠标拖动窗口事件
///
static bool mouse_press = false;
static QPointF mouse_point;

void XViewer::mouseMoveEvent(QMouseEvent *ev) {
  if (!mouse_press) {
    QWidget::mouseMoveEvent(ev);
  } else {
    this->move((ev->globalPosition() - mouse_point).toPoint());
  }
}

void XViewer::mousePressEvent(QMouseEvent *ev) {
  if (!(windowFlags() & Qt::FramelessWindowHint)) {
    QWidget::mousePressEvent(ev);
    return;
  }
  if (ev->button() == Qt::LeftButton) {
    mouse_press = true;
    mouse_point = ev->position();
  }
}

void XViewer::mouseReleaseEvent(QMouseEvent *ev) { mouse_press = false; }

void XViewer::SetupPlaybackUi() {
  auto *old = ui->playback_wid->layout();
  old->removeWidget(ui->time_list); old->removeWidget(ui->cal); delete old;
  auto label = [](const QString &text, QWidget *parent) {
    auto *value = new QLabel(text, parent); value->setTextFormat(Qt::PlainText);
    value->setWordWrap(true); return value;
  };
  date_filters_ = new QWidget(ui->left);
  auto *filters = new QVBoxLayout(date_filters_);
  filters->setContentsMargins(0, 8, 0, 0); filters->setSpacing(10);
  filters->setAlignment(Qt::AlignTop);
  filters->addWidget(label(QStringLiteral("录像日期"), date_filters_));
  ui->cal->setParent(date_filters_);
  ui->cal->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
  ui->cal->setHorizontalHeaderFormat(QCalendarWidget::SingleLetterDayNames);
  ui->cal->setFixedHeight(210);
  ui->cal->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  filters->addWidget(ui->cal);
  filters->addWidget(label(QStringLiteral("圆点表示当天有录像"), date_filters_));
  filters->addWidget(label(QStringLiteral("开始时间范围"), date_filters_));
  auto *times = new QHBoxLayout;
  filter_from_ = new QTimeEdit(QTime(0, 0), date_filters_);
  filter_to_ = new QTimeEdit(QTime(23, 59, 59), date_filters_);
  filter_from_->setObjectName(QStringLiteral("recordingFrom"));
  filter_to_->setObjectName(QStringLiteral("recordingTo"));
  for (auto *time : {filter_from_, filter_to_}) time->setButtonSymbols(QAbstractSpinBox::NoButtons);
  for (auto *time : {filter_from_, filter_to_}) time->setDisplayFormat(QStringLiteral("HH:mm:ss"));
  times->addWidget(filter_from_); times->addWidget(label(QStringLiteral("—"), date_filters_)); times->addWidget(filter_to_);
  filters->addLayout(times);
  auto *query = new QPushButton(QStringLiteral("查询录像"), date_filters_);
  query->setObjectName(QStringLiteral("queryRecordings")); filters->addWidget(query);
  connect(query, &QPushButton::clicked, this, [this] {
    if (filter_from_->time() > filter_to_->time()) {
      QMessageBox::information(this, QStringLiteral("时间范围"), QStringLiteral("开始时间不能晚于结束时间。")); return;
    }
    SelectDate(ui->cal->selectedDate());
  });
  recording_path_ = label({}, date_filters_);
  recording_path_->setTextInteractionFlags(Qt::TextSelectableByMouse); filters->addWidget(recording_path_);
  qobject_cast<QVBoxLayout *>(ui->left->layout())->addWidget(date_filters_);
  auto *root = new QVBoxLayout(ui->playback_wid);
  root->setContentsMargins(16, 12, 16, 12); root->setSpacing(14);
  auto *heading = new QHBoxLayout;
  auto *title = label(QStringLiteral("录像回放"), ui->playback_wid);
  title->setStyleSheet(QStringLiteral("font-size:22px;font-weight:600;color:#edf2f7;")); heading->addWidget(title, 1);
  scan_status_ = label({}, ui->playback_wid); heading->addWidget(scan_status_);
  auto *refresh = new QPushButton(QStringLiteral("刷新录像"), ui->playback_wid);
  refresh->setObjectName(QStringLiteral("refreshRecordings")); heading->addWidget(refresh);
  connect(refresh, &QPushButton::clicked, this, &XViewer::RefreshRecordings); root->addLayout(heading);
  auto *split = new QSplitter(Qt::Horizontal, ui->playback_wid);
  split->setChildrenCollapsible(false); root->addWidget(split, 1);
  auto *preview = new QWidget(split);
  auto *previewLayout = new QVBoxLayout(preview);
  previewLayout->setContentsMargins(0, 0, 12, 0); previewLayout->setSpacing(12);
  previewLayout->addWidget(label(QStringLiteral("回放预览"), preview));
  playback_player_ = new player_sdk_qt::QtPlayer(this);
  playback_view_ = new player_sdk_qt::VideoWidget(preview);
  playback_view_->setObjectName(QStringLiteral("recordingVideo")); playback_view_->setPlayer(playback_player_);
  playback_view_->setMinimumSize(280, 180);
  playback_view_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding); previewLayout->addWidget(playback_view_, 1);
  playback_position_ = new QSlider(Qt::Horizontal, preview);
  playback_position_->setRange(0, 10000); playback_position_->setObjectName(QStringLiteral("recordingPosition"));
  previewLayout->addWidget(playback_position_);
  auto *controls = new QHBoxLayout;
  playback_pause_ = new QPushButton(QStringLiteral("播放"), preview);
  playback_pause_->setObjectName(QStringLiteral("recordingPause"));
  auto *stop = new QPushButton(QStringLiteral("关闭"), preview);
  controls->addWidget(playback_pause_); controls->addWidget(stop);
  playback_clock_ = label({}, preview); controls->addWidget(playback_clock_, 1);
  auto *speed = new QComboBox(preview); speed->setObjectName(QStringLiteral("recordingSpeed"));
  for (double rate : {0.2, 0.5, 1.0, 1.5, 2.0}) speed->addItem(QString::number(rate, 'f', 1) + QStringLiteral("×"), rate);
  speed->setCurrentIndex(2); controls->addWidget(speed);
  auto *mute = new QPushButton(QStringLiteral("静音"), preview);
  mute->setCheckable(true); controls->addWidget(mute);
  connect(mute, &QPushButton::toggled, this, [this](bool muted) { playback_player_->setVolume(muted ? 0.0 : 1.0); });
  auto *fullscreen = new QPushButton(QStringLiteral("全屏"), preview); controls->addWidget(fullscreen);
  previewLayout->addLayout(controls);
  playback_file_ = label({}, preview); playback_file_->setObjectName(QStringLiteral("recordingFile"));
  playback_file_->setMinimumHeight(40); previewLayout->addWidget(playback_file_);
  previewLayout->addWidget(label(QStringLiteral("双击录像回放 · 时长在文件打开后读取"), preview));
  auto *listPanel = new QWidget(split); listPanel->setMinimumWidth(230);
  auto *listLayout = new QVBoxLayout(listPanel); listLayout->setContentsMargins(12, 0, 0, 0); listLayout->setSpacing(12);
  listLayout->addWidget(label(QStringLiteral("录制列表 · 按开始时间排序"), listPanel));
  recording_summary_ = label({}, listPanel); recording_summary_->setObjectName(QStringLiteral("recordingSummary"));
  listLayout->addWidget(recording_summary_);
  ui->time_list->setMaximumWidth(QWIDGETSIZE_MAX); ui->time_list->setMinimumWidth(0);
  ui->time_list->setTextElideMode(Qt::ElideMiddle); ui->time_list->setSpacing(4); listLayout->addWidget(ui->time_list, 1);
  recording_empty_ = label({}, listPanel); recording_empty_->setObjectName(QStringLiteral("recordingEmpty"));
  listLayout->addWidget(recording_empty_);
  split->setStretchFactor(0, 3); split->setStretchFactor(1, 1); split->setSizes({650, 280});
  connect(stop, &QPushButton::clicked, this, &XViewer::ResetPlayback);
  connect(playback_pause_, &QPushButton::clicked, this, [this] {
    const auto state = playback_player_->state();
    if (state == play_core::PlayerState::Ended) {
      playback_player_->open(QUrl::fromLocalFile(playback_path_)); playback_player_->play();
    } else playback_player_->setPaused(state == play_core::PlayerState::Playing);
  });
  connect(speed, &QComboBox::currentIndexChanged, this, [this, speed] { playback_player_->setPlaybackRate(speed->currentData().toDouble()); });
  connect(fullscreen, &QPushButton::clicked, this, [this, fullscreen] {
    if (isFullScreen()) { showNormal(); fullscreen->setText(QStringLiteral("全屏")); }
    else { showFullScreen(); fullscreen->setText(QStringLiteral("退出全屏")); }
  });
  auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  connect(escape, &QShortcut::activated, this, [this, fullscreen] {
    if (isFullScreen()) { showNormal(); fullscreen->setText(QStringLiteral("全屏")); }
  });
  connect(playback_position_, &QSlider::sliderReleased, this, [this] {
    if (playback_duration_ > 0) playback_player_->seek(playback_duration_ * playback_position_->value() / 10000);
  });
  connect(playback_player_, &player_sdk_qt::QtPlayer::mediaInfoChanged, this, [this](auto info) {
    playback_duration_ = info && info->duration ? info->duration->count() : 0;
    playback_position_->setEnabled(info && info->seekable && playback_duration_ > 0);
  });
  connect(playback_player_, &player_sdk_qt::QtPlayer::positionChanged, this, [this](qint64 ms) {
    auto format = [](qint64 value) {
      const auto sec = qMax<qint64>(0, value / 1000);
      return QStringLiteral("%1:%2:%3").arg(sec / 3600, 2, 10, QLatin1Char('0')).arg(sec / 60 % 60, 2, 10, QLatin1Char('0')).arg(sec % 60, 2, 10, QLatin1Char('0'));
    };
    playback_clock_->setText(format(ms) + QStringLiteral(" / ") + (playback_duration_ > 0 ? format(playback_duration_) : QStringLiteral("--:--")));
    if (!playback_position_->isSliderDown() && playback_duration_ > 0)
      playback_position_->setValue(static_cast<int>(qBound<qint64>(0LL, ms * 10000 / playback_duration_, 10000LL)));
  });
  connect(playback_player_, &player_sdk_qt::QtPlayer::stateChanged, this, [this](auto state) {
    playback_pause_->setEnabled(state == play_core::PlayerState::Playing || state == play_core::PlayerState::Paused || state == play_core::PlayerState::Ready || state == play_core::PlayerState::Ended);
    playback_pause_->setText(state == play_core::PlayerState::Playing ? QStringLiteral("暂停") : state == play_core::PlayerState::Ended ? QStringLiteral("重播") : QStringLiteral("播放"));
  });
  connect(playback_player_, &player_sdk_qt::QtPlayer::errorOccurred, this, [this](const QString &error) { playback_file_->setText(QStringLiteral("回放失败：%1").arg(error)); });
  setStyleSheet(styleSheet() + QStringLiteral(R"(
    QWidget#playback_wid { background:#101419; }
    QListWidget { background:#181e25; border:1px solid #2b3541; border-radius:6px; }
    QListWidget::item { padding:8px; }
    QListWidget::item:selected { background:#193d32; color:#56d6a0; }
    QTimeEdit, QComboBox { background:#202832;color:#edf2f7;border:1px solid #2b3541;border-radius:4px;padding:4px; }
    QSplitter::handle { background:#2b3541; }
    QSlider::groove:horizontal { height:6px;background:#2b3541;border-radius:3px; }
    QSlider::sub-page:horizontal { background:#56d6a0;border-radius:3px; }
    QSlider::handle:horizontal { background:#e6fff2;width:12px;margin:-3px 0;border-radius:6px; }
    QPushButton { padding:6px 10px; }
    QPushButton:disabled { color:#657181; }
    QCalendarWidget QToolButton { color:#edf2f7;background:#202832;border:0; }
    QCalendarWidget QAbstractItemView { selection-background-color:#193d32;selection-color:#56d6a0; }
  )"));
  for (auto *caption : ui->left->findChildren<QLabel *>())
    caption->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  ResetPlayback();
}
