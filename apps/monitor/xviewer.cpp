#include "xviewer.h"
#include "ui_XViewer.h"
#include "xcamera_config.h"
#include "xcamera_record.h"
#include "xcamera_widget.h"
#include "onvif_camera.h"
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
#include <QSettings>
#include <QScrollArea>
#include <QUrl>
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
  for (qsizetype i = 0; i < sources.size(); ++i) {
    if (!cam_wids[i]->open(sources[i], true)) return false;
    emit cam_wids[i]->cameraAssigned(QUrl(sources[i]).isLocalFile() || QUrl(sources[i]).scheme().size() <= 1
        ? QFileInfo(sources[i]).fileName() : QStringLiteral("网络视频"));
  }
  return true;
}

void XViewer::view(int count) {
  qDebug() << "View" << count;
  for (auto *button : findChildren<QPushButton *>()) {
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
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setHorizontalSpacing(12);
  lay->setVerticalSpacing(12);
  current_layout_ = count;
  if (layout_summary_) layout_summary_->setText(QStringLiteral("当前显示 %1 路 / 最大 16 路").arg(count));
  for (int row = 0; row < 4; ++row)
    lay->setRowStretch(row, row < rows ? 1 : 0);
  for (int col = 0; col < 4; ++col)
    lay->setColumnStretch(col, col < cols ? 1 : 0);
  //
  int i = 0;
  for (; i < count; ++i) {
    if (!cam_wids[i]) {
      cam_wids[i] = new XCameraWidget(this);
      cam_wids[i]->setObjectName(QStringLiteral("preview%1").arg(i + 1));
      cam_wids[i]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
      auto *card = new QWidget(ui->cams);
      card->setObjectName(QStringLiteral("channelCard"));
      card->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
      channel_cards_[i] = card;
      auto *cardLayout = new QVBoxLayout(card);
      cardLayout->setContentsMargins(1, 1, 1, 1);
      cardLayout->setSpacing(0);
      auto *title = new QLabel(QStringLiteral("%1    未分配通道").arg(i + 1, 2, 10, QLatin1Char('0')), card);
      title->setObjectName(QStringLiteral("channelTitle"));
      title->setFixedHeight(24);
      title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
      title->setContentsMargins(6, 0, 6, 0);
      auto *header = new QHBoxLayout;
      header->setContentsMargins(0, 0, 2, 0);
      header->setSpacing(4);
      header->addWidget(title, 1);
      auto *closePreview = new QPushButton(QStringLiteral("×"), card);
      closePreview->setObjectName(QStringLiteral("closePreview%1").arg(i + 1));
      closePreview->setProperty("previewClose", true);
      closePreview->setFixedSize(22, 22);
      closePreview->setAccessibleName(QStringLiteral("关闭预览"));
      closePreview->setToolTip(QStringLiteral("仅关闭此路预览，不影响设备配置和录制"));
      header->addWidget(closePreview);
      cardLayout->addLayout(header);
      cardLayout->addWidget(cam_wids[i], 1);
      auto *state = new QLabel(card);
      state->setObjectName(QStringLiteral("channelState"));
      state->setFixedHeight(18);
      state->setContentsMargins(6, 0, 6, 0);
      cardLayout->addWidget(state);
      state->hide();
      connect(cam_wids[i], &XCameraWidget::cameraAssigned, title, [title, i](const QString &name) {
        title->setText(QStringLiteral("%1    %2").arg(i + 1, 2, 10, QLatin1Char('0')).arg(name));
      });
      connect(cam_wids[i], &CoreVideoWidget::playbackError, state, [state](const QString &) {
        state->setProperty("failed", true);
        state->setText(QStringLiteral("连接失败 · 请检查设备地址或网络"));
        state->show();
      });
      connect(cam_wids[i], &XCameraWidget::cameraAssigned, state, [state] {
        state->setProperty("failed", false);
        state->setText(QStringLiteral("正在连接…"));
        state->show();
      });
      connect(closePreview, &QPushButton::clicked, cam_wids[i], &XCameraWidget::stop);
      connect(cam_wids[i], &XCameraWidget::previewClosed, card, [title, state, i] {
        title->setText(QStringLiteral("%1    未分配通道").arg(i + 1, 2, 10, QLatin1Char('0')));
        state->setProperty("failed", false);
        state->clear();
        state->hide();
      });
    }
    lay->addWidget(channel_cards_[i], i / rows, i % cols);
  }
  //
  for (; i < wid_seze; ++i) {
    if (cam_wids[i]) {
      delete channel_cards_[i];
      channel_cards_[i] = nullptr;
      cam_wids[i] = nullptr;
    }
  }
  QSettings settings(QStringLiteral("MyPlayer"), QStringLiteral("Monitor"));
  if (settings.value(QStringLiteral("live/layout"), 0).toInt() != count) {
    settings.setValue(QStringLiteral("live/layout"), count);
    settings.sync();
    if (settings.status() != QSettings::NoError)
      ui->status->setText(QStringLiteral("分屏布局自动保存失败，请检查设置写入权限"));
  }
}

void XViewer::RefreshCams() {
  auto c = XCameraConfig::Instance();
  std::map<int, Qt::CheckState> checked;
  const int current = ui->cam_list->currentRow();
  for (int row = 0; row < ui->cam_list->count(); ++row) {
    const auto *item = ui->cam_list->item(row);
    checked[item->data(Qt::UserRole).toInt()] = item->checkState();
  }
  {
    const QSignalBlocker blocker(ui->cam_list);
    ui->cam_list->clear();
    const int count = c->GetCamCount();
    for (int i = 0; i < count; ++i) {
      const auto cam = c->GetCam(i);
      auto *item = new QListWidgetItem(QIcon(":/img/cam.png"), C(cam.name), ui->cam_list);
      item->setSizeHint(QSize(180, 78));
      item->setData(Qt::UserRole, i);
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      const auto found = checked.find(i);
      item->setCheckState(found == checked.end() ? Qt::Unchecked : found->second);
    }
    if (current >= 0 && count > 0)
      ui->cam_list->setCurrentRow(qMin(current, count - 1));
    if (device_summary_)
      device_summary_->setText(QStringLiteral("全部设备    /    %1 台已配置").arg(count));
    RefreshRecordingState();
  }
  if (recording_summary_ && ui->playback_wid->isVisible())
    RefreshRecordings();
}

void XViewer::RefreshRecordingState() {
  for (int row = 0; row < ui->cam_list->count(); ++row) {
    auto *item = ui->cam_list->item(row);
    const int index = item->data(Qt::UserRole).toInt();
    const auto cam = XCameraConfig::Instance()->GetCam(index);
    const QString host = QUrl(C(cam.url)).host();
    const bool active = records.contains(index);
    item->setText(QStringLiteral("%1\nCAM-%2%3\n%4")
        .arg(C(cam.name)).arg(index + 1, 2, 10, QLatin1Char('0'))
        .arg(host.isEmpty() ? QString() : QStringLiteral(" · %1").arg(host))
        .arg(active ? QStringLiteral("录制任务运行中 · 连接/重试")
                    : QStringLiteral("未启动录制")));
    item->setToolTip(active
        ? QStringLiteral("%1\n录制任务已启动；连接、写入或重试由后台处理，尚未确认文件写入。").arg(C(cam.name))
        : QStringLiteral("%1\n勾选仅指定录制操作目标，不会自动启停。").arg(C(cam.name)));
  }
}

void XViewer::SetCam(int index) {
  if (records.contains(index)) {
    ui->status->setText(QStringLiteral("请先停止该设备的录制任务，再编辑配置。"));
    return;
  }
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
    auto *discover = new QPushButton(QStringLiteral("发现摄像头"), &dlg);
    discover->setAutoDefault(false);
    lay->insertRow(0, QString(), discover);
    connect(discover, &QPushButton::clicked, &dlg, [&] {
      if (const auto camera = discoverOnvifCamera(&dlg)) {
        name_edit->setText(camera->name);
        url_edit->setText(camera->mainStream);
        suburl_edit->setText(camera->subStream);
        save_path_edit->setFocus();
      }
    });
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
  setWindowTitle(QStringLiteral("MyPlayer Monitor"));
  setWindowIcon(QIcon(QStringLiteral(":/img/xv.ico")));
  const QSize available = QGuiApplication::primaryScreen()->availableGeometry().size();
  resize(qMin(1600, available.width() - 40), qMin(1000, available.height() - 60));
  setMinimumSize(1100, 680);
  setStyleSheet(R"(
    QWidget { color: #cad5df; font-family: "Microsoft YaHei UI"; font-size: 12px; }
    QWidget#XViewer, QWidget#body, QWidget#cams, QWidget#liveWorkspace { background: #10171f; }
    QWidget#head { background: #181e25; }
    QWidget#left, QWidget#liveTools, QWidget#toolContents { background: #171f29; }
    QLabel { background: transparent; }
    QLabel#sectionTitle { color: #edf2f7; font-size: 15px; font-weight: bold; }
    QLabel#muted { color: #8395a7; font-size: 11px; }
    QPushButton { color: #bfcdd9; background: #24303d; border: 1px solid #303b48; border-radius: 5px; padding: 6px 10px; }
    QPushButton:hover { background: #303e4c; border-color: #526575; }
    QPushButton:checked { background: #233c31; border-color: #56d6a0; color: #78e1b2; }
    QPushButton:disabled { color: #718497; background: #1c2631; border-color: #283441; }
    QPushButton#preview, QPushButton#playback { background: transparent; border: none; border-radius: 0; padding: 14px 8px; color: #9baabb; }
    QPushButton#preview:checked, QPushButton#playback:checked { color: #6fe0af; border-bottom: 2px solid #56d6a0; }
    QLineEdit { background: #222d39; color: #dce6ef; border: 1px solid #303b48; border-radius: 5px; padding: 9px; }
    QListWidget { background: #171f29; color: #cad5df; border: none; outline: none; }
    QListWidget::item { padding: 8px; border-radius: 6px; }
    QListWidget::item:selected { background: #233c31; color: #78e1b2; }
    QListWidget::item:hover { background: #222d39; }
    QMenu { background: #222d39; color: #dce6ef; border: 1px solid #303b48; }
    QMenu::item { padding: 7px 20px; }
    QMenu::item:selected { background: #233c31; }
    QWidget#channelCard { background: #181e25; border: 1px solid #303b48; border-radius: 6px; }
    QLabel#channelTitle { color: #e5edf3; background: #222d39; font-weight: bold; }
    QPushButton[previewClose="true"] { padding: 0; border: none; border-radius: 3px; font-size: 16px; }
    QLabel#channelState { color: #8395a7; background: #181e25; font-size: 11px; }
    QScrollArea { border: none; }
    QCalendarWidget QWidget { background: #1b2530; color: #cad5df; }
  )");
  auto *thisVLayout = new QVBoxLayout(this);
  thisVLayout->setContentsMargins(0, 0, 0, 0);
  thisVLayout->setSpacing(0);
  thisVLayout->addWidget(ui->head);
  thisVLayout->addWidget(ui->body, 1);

  ui->logo->hide();
  ui->min->hide();
  ui->max->hide();
  ui->normal->hide();
  ui->close->hide();
  ui->head->setFixedHeight(64);
  ui->left->setFixedWidth(232);
  ui->cam_list->setIconSize(QSize(18, 18));
  ui->add_cam->setText(QStringLiteral("＋ 添加"));
  ui->set_cam->setText(QStringLiteral("编辑"));
  ui->del_cam->setText(QStringLiteral("删除"));
  ui->status->setText(QStringLiteral("未开始录像"));
  ui->status->setStyleSheet(QStringLiteral("color:#9baabb;"));
  ui->preview->setText(QStringLiteral("实时监控"));
  ui->playback->setText(QStringLiteral("录像回放"));

  auto *headLayout = new QHBoxLayout(ui->head);
  headLayout->setContentsMargins(24, 0, 24, 0);
  headLayout->setSpacing(22);
  auto *mark = new QLabel(QStringLiteral("M"), ui->head);
  mark->setFixedSize(30, 30);
  mark->setAlignment(Qt::AlignCenter);
  mark->setStyleSheet(QStringLiteral("background:#56d6a0;color:#10271d;border-radius:8px;font-size:19px;font-weight:bold;"));
  headLayout->addWidget(mark);
  auto *brand = new QLabel(QStringLiteral("MyPlayer Monitor"), ui->head);
  brand->setStyleSheet(QStringLiteral("font-size:17px;font-weight:bold;color:#edf2f7;"));
  headLayout->addWidget(brand);
  headLayout->addSpacing(8);
  headLayout->addWidget(ui->preview);
  headLayout->addWidget(ui->playback);
  auto *alarmNav = new QPushButton(QStringLiteral("告警中心"), ui->head);
  alarmNav->setEnabled(false);
  alarmNav->setToolTip(QStringLiteral("尚未接入告警服务"));
  headLayout->addWidget(alarmNav);
  headLayout->addStretch();
  auto *clock = new QLabel(ui->head);
  clock->setObjectName(QStringLiteral("muted"));
  headLayout->addWidget(clock);

  auto *sideLayout = qobject_cast<QVBoxLayout *>(ui->left->layout());
  sideLayout->setContentsMargins(16, 16, 16, 16);
  sideLayout->setSpacing(12);
  auto *deviceTitle = new QLabel(QStringLiteral("设备资源"), ui->left);
  deviceTitle->setObjectName(QStringLiteral("sectionTitle"));
  sideLayout->insertWidget(0, deviceTitle);
  auto *search = new QLineEdit(ui->left);
  search->setObjectName(QStringLiteral("deviceSearch"));
  search->setPlaceholderText(QStringLiteral("搜索设备名称 / IP"));
  search->setClearButtonEnabled(true);
  sideLayout->insertWidget(1, search);
  device_summary_ = new QLabel(ui->left);
  device_summary_->setStyleSheet(QStringLiteral("color:#72dfae;"));
  sideLayout->insertWidget(2, device_summary_);
  connect(search, &QLineEdit::textChanged, this, [this](const QString &text) {
    auto *config = XCameraConfig::Instance();
    for (int i = 0; i < ui->cam_list->count(); ++i) {
      auto *item = ui->cam_list->item(i);
      const auto camera = config->GetCam(i);
      const QString host = QUrl(C(camera.url)).host();
      item->setHidden(!item->text().contains(text, Qt::CaseInsensitive) && !host.contains(text, Qt::CaseInsensitive));
    }
  });
  for (auto *button : {ui->add_cam, ui->set_cam, ui->del_cam}) {
    button->setMinimumWidth(0);
    button->setStyleSheet(QStringLiteral("padding:6px;"));
  }
  ui->horizontalLayout->setSpacing(4);
  auto *refreshDevices = new QPushButton(QStringLiteral("↻  刷新设备"), ui->left);
  refreshDevices->setObjectName(QStringLiteral("refreshDevices"));
  sideLayout->addWidget(refreshDevices);
  connect(refreshDevices, &QPushButton::clicked, this, [this, search] {
    RefreshCams();
    emit search->textChanged(search->text());
  });

  live_workspace_ = new QWidget(ui->body);
  live_workspace_->setObjectName(QStringLiteral("liveWorkspace"));
  auto *workspace = new QVBoxLayout(live_workspace_);
  workspace->setContentsMargins(20, 20, 20, 20);
  workspace->setSpacing(16);
  workspace->addWidget(ui->cams, 1);

  auto *toolbar = new QHBoxLayout;
  toolbar->setSpacing(8);
  toolbar->addWidget(new QLabel(QStringLiteral("画面布局"), live_workspace_));
  auto *layoutButtons = new QButtonGroup(this);
  for (int count : {1, 4, 9, 16}) {
    auto *button = new QPushButton(QString::number(count), live_workspace_);
    button->setObjectName(QStringLiteral("layout%1").arg(count));
    button->setCheckable(true);
    button->setProperty("layoutCount", count);
    layoutButtons->addButton(button);
    toolbar->addWidget(button);
    connect(button, &QPushButton::clicked, this, [this, count] { view(count); });
  }
  toolbar->addStretch();
  auto *recordButton = new QPushButton(QStringLiteral("● 录制勾选"), live_workspace_);
  recordButton->setObjectName(QStringLiteral("startSelectedRecording"));
  toolbar->addWidget(recordButton);
  connect(recordButton, &QPushButton::clicked, this, &XViewer::StartRecord);
  auto *stopButton = new QPushButton(QStringLiteral("停止勾选"), live_workspace_);
  stopButton->setObjectName(QStringLiteral("stopSelectedRecording"));
  toolbar->addWidget(stopButton);
  connect(stopButton, &QPushButton::clicked, this, &XViewer::StopSelectedRecord);
  auto *stopAllButton = new QPushButton(QStringLiteral("全部停止"), live_workspace_);
  stopAllButton->setObjectName(QStringLiteral("stopAllRecording"));
  toolbar->addWidget(stopAllButton);
  connect(stopAllButton, &QPushButton::clicked, this, &XViewer::StopRecord);
  auto *fullscreen = new QPushButton(QStringLiteral("⛶ 全屏"), live_workspace_);
  toolbar->addWidget(fullscreen);
  connect(fullscreen, &QPushButton::clicked, this, [this] {
    isFullScreen() ? showNormal() : showFullScreen();
  });
  auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  connect(escape, &QShortcut::activated, this, [this] { if (isFullScreen()) showNormal(); });
  workspace->addLayout(toolbar);

  auto *tools = new QScrollArea(ui->body);
  live_tools_ = tools;
  tools->setObjectName(QStringLiteral("liveTools"));
  tools->setFixedWidth(252);
  tools->setWidgetResizable(true);
  tools->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *toolContents = new QWidget;
  toolContents->setObjectName(QStringLiteral("toolContents"));
  tools->setWidget(toolContents);
  auto *toolLayout = new QVBoxLayout(toolContents);
  toolLayout->setContentsMargins(16, 20, 16, 20);
  toolLayout->setSpacing(16);
  auto *alarmTitle = new QLabel(QStringLiteral("实时告警"), toolContents);
  alarmTitle->setObjectName(QStringLiteral("sectionTitle"));
  toolLayout->addWidget(alarmTitle);
  auto *alarmEmpty = new QLabel(QStringLiteral("尚未接入告警服务\n\n当前仅提供视频预览与录像，\n不生成模拟告警。"), toolContents);
  alarmEmpty->setObjectName(QStringLiteral("muted"));
  alarmEmpty->setWordWrap(true);
  alarmEmpty->setMinimumHeight(140);
  toolLayout->addWidget(alarmEmpty);
  auto *ptzTitle = new QLabel(QStringLiteral("云台控制"), toolContents);
  ptzTitle->setObjectName(QStringLiteral("sectionTitle"));
  toolLayout->addWidget(ptzTitle);
  auto *ptzHint = new QLabel(QStringLiteral("当前设备未接入云台协议"), toolContents);
  ptzHint->setObjectName(QStringLiteral("muted"));
  toolLayout->addWidget(ptzHint);
  auto *ptz = new QGridLayout;
  const QStringList directions{QStringLiteral("↖"), QStringLiteral("↑"), QStringLiteral("↗"),
      QStringLiteral("←"), QStringLiteral("●"), QStringLiteral("→"),
      QStringLiteral("↙"), QStringLiteral("↓"), QStringLiteral("↘")};
  for (int i = 0; i < directions.size(); ++i) {
    auto *button = new QPushButton(directions[i], toolContents);
    button->setMinimumHeight(36);
    button->setEnabled(false);
    ptz->addWidget(button, i / 3, i % 3);
  }
  toolLayout->addLayout(ptz);
  auto *zoom = new QHBoxLayout;
  zoom->addWidget(new QLabel(QStringLiteral("变倍"), toolContents));
  for (const auto &text : {QStringLiteral("−"), QStringLiteral("＋")}) {
    auto *button = new QPushButton(text, toolContents);
    button->setEnabled(false);
    zoom->addWidget(button);
  }
  toolLayout->addLayout(zoom);
  toolLayout->addStretch();
  auto *operations = new QLabel(QStringLiteral("操作提示\n\n从左侧添加设备，配置视频地址\n及录像保存目录后拖入画面。"), toolContents);
  operations->setObjectName(QStringLiteral("muted"));
  operations->setWordWrap(true);
  toolLayout->addWidget(operations);

  auto *bodyLayout = new QHBoxLayout(ui->body);
  bodyLayout->setContentsMargins(0, 0, 0, 0);
  bodyLayout->setSpacing(0);
  bodyLayout->addWidget(ui->left);
  bodyLayout->addWidget(live_workspace_, 1);
  bodyLayout->addWidget(ui->playback_wid, 1);
  bodyLayout->addWidget(live_tools_);
  auto *footer = new QHBoxLayout;
  footer->setContentsMargins(16, 5, 16, 5);
  footer->addWidget(ui->status);
  footer->addSpacing(20);
  layout_summary_ = new QLabel(this);
  layout_summary_->setObjectName(QStringLiteral("muted"));
  footer->addWidget(layout_summary_);
  footer->addStretch();
  auto *connection = new QLabel(this);
  connection->setObjectName(QStringLiteral("muted"));
  footer->addWidget(connection);
  thisVLayout->addLayout(footer);
  auto *timer = new QTimer(this);
  auto updateStatus = [this, clock, connection] {
    clock->setText(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd  HH:mm:ss")));
    int withFrames = 0;
    for (int i = 0; i < current_layout_; ++i) {
      if (!cam_wids[i]) continue;
      auto *state = channel_cards_[i]->findChild<QLabel *>(QStringLiteral("channelState"));
      if (cam_wids[i]->hasFrame()) {
        ++withFrames;
        if (!state->property("failed").toBool()) state->hide();
      }
    }
    connection->setText(QStringLiteral("已收到画面 %1 / %2 路").arg(withFrames).arg(current_layout_));
  };
  connect(timer, &QTimer::timeout, this, updateStatus);
  timer->start(1000);
  updateStatus();
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
  a = left_menu_->addAction("录制勾选设备");
  connect(a, &QAction::triggered, this, &XViewer::StartRecord);
  a = left_menu_->addAction("停止勾选设备");
  connect(a, &QAction::triggered, this, &XViewer::StopSelectedRecord);
  a = left_menu_->addAction("全部停止录制");
  connect(a, &QAction::triggered, this, &XViewer::StopRecord);
  const int savedLayout = QSettings(QStringLiteral("MyPlayer"), QStringLiteral("Monitor")).value(QStringLiteral("live/layout"), 4).toInt();
  view(savedLayout == 1 || savedLayout == 4 || savedLayout == 9 || savedLayout == 16 ? savedLayout : 4);
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
  if (!records.empty()) {
    ui->status->setText(QStringLiteral("请先停止全部录制任务，再删除设备，以免设备索引变化。"));
    return;
  }
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
    delete ui->cam_list->takeItem(row);
    for (int i = row; i < ui->cam_list->count(); ++i)
      ui->cam_list->item(i)->setData(Qt::UserRole, i);
    if (!c->Save())
      QMessageBox::warning(this, QStringLiteral("配置保存失败"),
          QStringLiteral("无法保存设备配置，请检查目录写入权限：\n%1\n本次修改仅在当前进程中生效。")
              .arg(XCameraConfig::DefaultPath()));
    RefreshCams();
  }
}

void XViewer::StartRecord() {
  auto *conf = XCameraConfig::Instance();
  int selected = 0;
  int started = 0;
  QStringList failures;
  for (int row = 0; row < ui->cam_list->count(); ++row) {
    const auto *item = ui->cam_list->item(row);
    if (item->checkState() != Qt::Checked) continue;
    ++selected;
    const int index = item->data(Qt::UserRole).toInt();
    if (records.contains(index)) continue;
    const auto cam = conf->GetCam(index);
    const QString path = QDir(C(cam.save_path)).filePath(QString::number(index));
    if (!QDir().mkpath(path)) {
      failures.push_back(QStringLiteral("%1：无法创建目录 %2").arg(C(cam.name), QDir::toNativeSeparators(path)));
      continue;
    }
    auto *record = new XCameraRecord();
    record->set_rtsp_url(cam.url);
    record->set_save_path(path.toLocal8Bit().toStdString());
    record->set_file_sec(120);
    record->Start();
    records.emplace(index, record);
    ++started;
  }
  RefreshRecordingState();
  if (selected == 0)
    ui->status->setText(QStringLiteral("请先勾选要录制的设备。"));
  else if (!failures.isEmpty())
    ui->status->setText(QStringLiteral("已新增 %1 个录制任务；%2").arg(started).arg(failures.join(QStringLiteral("；"))));
  else
    ui->status->setText(QStringLiteral("新增 %1 个任务，共 %2 个任务运行中（连接/重试，未确认写入）；已有任务未重启。")
        .arg(started).arg(records.size()));
}

void XViewer::StopSelectedRecord() {
  int selected = 0;
  int stopped = 0;
  for (int row = 0; row < ui->cam_list->count(); ++row) {
    const auto *item = ui->cam_list->item(row);
    if (item->checkState() != Qt::Checked) continue;
    ++selected;
    const auto found = records.find(item->data(Qt::UserRole).toInt());
    if (found == records.end()) continue;
    delete found->second; // Destructor interrupts I/O and joins the worker.
    records.erase(found);
    ++stopped;
  }
  RefreshRecordingState();
  ui->status->setText(selected == 0 ? QStringLiteral("请先勾选要停止录制的设备。")
      : QStringLiteral("已停止 %1 个勾选任务，剩余 %2 个录制任务。").arg(stopped).arg(records.size()));
  if (recording_summary_ && ui->playback_wid->isVisible())
    RefreshRecordings();
}

void XViewer::StopRecord() {
  for (const auto &[index, record] : records)
    delete record;
  records.clear();
  RefreshRecordingState();
  ui->status->setText(QStringLiteral("全部录制任务已停止"));
  if (recording_summary_ && ui->playback_wid->isVisible())
    RefreshRecordings();
}

void XViewer::Preview() {
  ResetPlayback();
  date_filters_->hide();
  ui->cam_list->setMaximumHeight(QWIDGETSIZE_MAX);
  live_workspace_->show();
  live_tools_->show();
  ui->cams->show();
  ui->preview->setChecked(true);
  ui->playback_wid->hide();
}

void XViewer::Playback() {
  date_filters_->show();
  ui->cam_list->setMaximumHeight(100);
  live_workspace_->hide();
  live_tools_->hide();
  ui->playback->setChecked(true);
  ui->playback_wid->show();
  ui->cams->hide();
  if (ui->cam_list->currentRow() < 0 && ui->cam_list->count() > 0)
    ui->cam_list->setCurrentRow(0);
  RefreshRecordings();
}

void XViewer::RefreshRecordings() {
  SelectCamera(ui->cam_list->currentIndex());
}

void XViewer::SelectCamera(const QModelIndex &index) {
  ResetPlayback();
  const auto selectedDate = ui->cal->selectedDate();
  ui->cal->ClearDate();
  ui->time_list->clear();
  cam_videos.clear();
  recording_path_->setToolTip({});
  recording_path_->setEnabled(false);
  if (!index.isValid()) {
    SelectDate(selectedDate);
    recording_empty_->setText(QStringLiteral("请选择设备以查看录像。"));
    return;
  }
  const auto data = XCameraConfig::Instance()->GetCam(index.row());
  const QDir root(QString::fromLocal8Bit(data.save_path));
  const QDir dir(root.filePath(QString::number(index.row())));
  recording_path_->setEnabled(true);
  recording_path_->setToolTip(QDir::toNativeSeparators(dir.absolutePath()));
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
  playback_file_->setVisible(true);
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
  playback_file_->clear();
  playback_file_->setToolTip({});
  playback_file_->hide();
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
  ui->cal->setToolTip(QStringLiteral("圆点表示当天有录像"));
  filters->addWidget(label(QStringLiteral("开始时间"), date_filters_));
  auto *times = new QHBoxLayout;
  filter_from_ = new QTimeEdit(QTime(0, 0), date_filters_);
  filter_to_ = new QTimeEdit(QTime(23, 59, 59), date_filters_);
  filter_from_->setObjectName(QStringLiteral("recordingFrom"));
  filter_to_->setObjectName(QStringLiteral("recordingTo"));
  filter_from_->setToolTip(QStringLiteral("录像开始时间下限"));
  filter_to_->setToolTip(QStringLiteral("录像开始时间上限"));
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
  recording_path_ = label(QStringLiteral("录像目录"), date_filters_);
  filters->addWidget(recording_path_);
  qobject_cast<QVBoxLayout *>(ui->left->layout())->addWidget(date_filters_);
  auto *root = new QVBoxLayout(ui->playback_wid);
  root->setContentsMargins(16, 12, 16, 12); root->setSpacing(8);
  auto *split = new QSplitter(Qt::Horizontal, ui->playback_wid);
  split->setChildrenCollapsible(false); root->addWidget(split, 1);
  auto *preview = new QWidget(split);
  auto *previewLayout = new QVBoxLayout(preview);
  previewLayout->setContentsMargins(0, 0, 8, 0); previewLayout->setSpacing(8);
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
  previewLayout->addWidget(playback_file_);
  auto *listPanel = new QWidget(split); listPanel->setMinimumWidth(230);
  auto *listLayout = new QVBoxLayout(listPanel);
  listLayout->setContentsMargins(8, 0, 0, 0); listLayout->setSpacing(8);
  auto *listHeader = new QHBoxLayout;
  listHeader->addWidget(label(QStringLiteral("录像"), listPanel), 1);
  auto *refresh = new QPushButton(QStringLiteral("刷新"), listPanel);
  refresh->setObjectName(QStringLiteral("refreshRecordings"));
  refresh->setToolTip(QStringLiteral("重新扫描所选设备的录像"));
  listHeader->addWidget(refresh);
  connect(refresh, &QPushButton::clicked, this, &XViewer::RefreshRecordings);
  listLayout->addLayout(listHeader);
  recording_summary_ = label({}, listPanel); recording_summary_->setObjectName(QStringLiteral("recordingSummary"));
  listLayout->addWidget(recording_summary_);
  ui->time_list->setMaximumWidth(QWIDGETSIZE_MAX); ui->time_list->setMinimumWidth(0);
  ui->time_list->setTextElideMode(Qt::ElideMiddle); ui->time_list->setSpacing(4); listLayout->addWidget(ui->time_list, 1);
  ui->time_list->setToolTip(QStringLiteral("双击录像开始回放"));
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
  connect(playback_player_, &player_sdk_qt::QtPlayer::errorOccurred, this, [this](const QString &error) {
    playback_file_->setText(QStringLiteral("回放失败：%1").arg(error));
    playback_file_->show();
  });
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
