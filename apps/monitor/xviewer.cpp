#include "xviewer.h"
#include "ui_XViewer.h"
#include "xcamera_config.h"
#include "xcamera_record.h"
#include "xcamera_widget.h"
#include "xplayvideo.h"
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
#include <QVBoxLayout>
#include <QDir>
#include <cmath>
#include <map>
#include <vector>
#include <cstring>
#include <iostream>
#include <sstream>
#define CAM_CONF_PATH "monitor-cams.db"
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
      !copyField(data.sub_url, suburl_edit->text()) || !copyField(data.save_path, save_path_edit->text()))
    return;
  if (index == -1) // 添加
    c->Push(data);
  else { // 修改
    c->SetCam(index, data);
  }
  c->Save(CAM_CONF_PATH);
  RefreshCams();
}

XViewer::XViewer(QWidget *parent) : QWidget{parent} {
  ui = new Ui::XViewer();
  ui->setupUi(this);
  setWindowFlags(Qt::Window);
  setWindowTitle(QStringLiteral("MyPlayer 多路监控"));
  setWindowIcon(QIcon(QStringLiteral(":/img/xv.ico")));
  const QSize available = QGuiApplication::primaryScreen()->availableGeometry().size();
  resize(qMin(1200, available.width() - 60), qMin(760, available.height() - 80));
  setMinimumSize(640, 400);
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
  ui->left->setMinimumWidth(140);
  ui->left->setMaximumWidth(150);
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
  sideLayout->setSpacing(5);
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
  XCameraConfig::Instance()->Load(CAM_CONF_PATH);
  ui->time_list->clear();
  RefreshCams();
  //
  connect(ui->add_cam, &QPushButton::clicked, this, &XViewer::AddCam);
  connect(ui->set_cam, SIGNAL(clicked()), this, SLOT(SetCam()));
  connect(ui->del_cam, &QPushButton::clicked, this, &XViewer::DelCam);
  connect(ui->preview, &QPushButton::clicked, this, &XViewer::Preview);
  connect(ui->playback, &QPushButton::clicked, this, &XViewer::Playback);
  Preview();
  connect(ui->cam_list, &QListWidget::clicked, this, &XViewer::SelectCamera);
  connect(ui->cal, &XCalendar::clicked, this, &XViewer::SelectDate);
  connect(ui->time_list, &QListWidget::doubleClicked, this,
          &XViewer::PlayVideo);
  //
}

XViewer::~XViewer() {
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
    c->Save(CAM_CONF_PATH);
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
}

void XViewer::Preview() {
  ui->cams->show();
  ui->playback_wid->hide();
}

void XViewer::Playback() {
  ui->playback_wid->show();
  ui->cams->hide();
}

void XViewer::SelectCamera(const QModelIndex &index) {
  auto conf = XCameraConfig::Instance();
  auto data = conf->GetCam(index.row());
  if (data.name[0] == '\0') {
    return;
  }
  stringstream ss;
  ss << data.save_path << '/' << index.row() << '/';
  QDir dir(QString::fromStdString(ss.str()));
  ui->cal->ClearDate();
  ui->time_list->clear();
  cam_videos.clear();
  if (!dir.exists())
    return;
  // 获取文件列表
  QStringList fileters;
  fileters << "*.mp4" << "*.avi";
  auto files = dir.entryInfoList(fileters, QDir::Filter::Files |
                                               QDir::Filter::NoDotAndDotDot);
  for (auto &file : std::as_const(files)) {
    qDebug() << file.fileName();
    auto s = file.baseName();
    s = s.right(s.length() - 4);
    auto dateTime = QDateTime::fromString(s.left(19), "yyyy_MM_dd_hh_mm_ss");
    if (!dateTime.isValid())
      continue;
    // qDebug() << dateTime.date();
    ui->cal->AddDate(dateTime.date());
    XCamVideo video;
    video.datetime = dateTime;
    video.filepath = file.absoluteFilePath();
    cam_videos[dateTime.date()].push_back(video);
  }
}

void XViewer::SelectDate(QDate date) {
  auto dates = cam_videos[date];
  ui->time_list->clear();
  for (auto &d : std::as_const(dates)) {
    auto item = new QListWidgetItem(d.datetime.time().toString());
    // 添加路径
    item->setData(Qt::UserRole, d.filepath);
    ui->time_list->addItem(item);
  }
}

void XViewer::PlayVideo(const QModelIndex &index) {
  auto item = ui->time_list->currentItem();
  if (!item)
    return;
  QString path = item->data(Qt::UserRole).toString();
  qDebug() << path;
  XPlayVideo play;
  play.Open(path.toLocal8Bit().constData());
  play.exec();
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
