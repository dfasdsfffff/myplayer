#include "review_window.h"
#include "review_compare.h"
#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSlider>
#include <QStackedWidget>
#include <QToolBar>
#include <QUndoStack>
#include <QVBoxLayout>
#include <player_sdk_qt/video_widget.h>

namespace review {
void ReviewWindow::buildMediaTools()
{
    compare_ = new ReviewCompare(videoStack_);
    compare_->setObjectName(QStringLiteral("reviewCompare"));
    videoStack_->addWidget(compare_);
    connect(compare_, &ReviewCompare::errorOccurred, this, &ReviewWindow::playbackError);
    auto* dock = new QDockWidget(tr("素材 / A/B"), this);
    dock->setObjectName(QStringLiteral("reviewMediaDock"));
    auto* panel = new QWidget(dock);
    auto* layout = new QVBoxLayout(panel);
    auto* tools = new QToolBar(panel);
    auto* add = tools->addAction(tr("添加"));
    add->setObjectName(QStringLiteral("addMediaAction"));
    connect(add, &QAction::triggered, this, [this] {
        const auto paths = QFileDialog::getOpenFileNames(this, tr("添加本地素材"), {}, tr("所有文件 (*)"));
        for (const auto& path : paths) addMedia(path);
    });
    auto* remove = tools->addAction(tr("移除"));
    remove->setObjectName(QStringLiteral("removeMediaAction"));
    connect(remove, &QAction::triggered, this, &ReviewWindow::removeSelectedMedia);
    layout->addWidget(tools);
    mediaList_ = new QListWidget(panel);
    mediaList_->setObjectName(QStringLiteral("reviewMediaList"));
    mediaList_->setToolTip(tr("双击切换 A；切换前保存或放弃当前标注。素材列表仅本次工作区有效。"));
    connect(mediaList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { activateMedia(mediaList_->row(item)); });
    layout->addWidget(mediaList_, 1);
    auto* roles = new QHBoxLayout;
    auto* a = new QPushButton(tr("设为 A"), panel);
    auto* b = new QPushButton(tr("设为 B"), panel);
    a->setObjectName(QStringLiteral("setPrimaryMediaButton"));
    b->setObjectName(QStringLiteral("setSecondaryMediaButton"));
    connect(a, &QPushButton::clicked, this, [this] { activateMedia(mediaList_->currentRow()); });
    connect(b, &QPushButton::clicked, this, [this] { compareMedia(mediaList_->currentRow()); });
    roles->addWidget(a); roles->addWidget(b); layout->addLayout(roles);
    compareMode_ = new QComboBox(panel);
    compareMode_->setObjectName(QStringLiteral("compareModeCombo"));
    compareMode_->addItems({tr("仅 A"), tr("A/B 并排"), tr("擦除"), tr("叠加"), tr("差分")});
    connect(compareMode_, &QComboBox::currentIndexChanged, this, [this](int index) {
        comparisonEnabled_ = index > 0;
        if (index > 0) compare_->setMode(static_cast<ReviewCompare::Mode>(index - 1));
        updateCompareView();
    });
    layout->addWidget(compareMode_);
    auto* amount = new QSlider(Qt::Horizontal, panel);
    amount->setObjectName(QStringLiteral("compareAmountSlider"));
    amount->setRange(0, 100); amount->setValue(50);
    amount->setToolTip(tr("擦除位置 / B 叠加比例"));
    amount->setAccessibleName(tr("擦除位置 / B 叠加比例"));
    compare_->setOverlayAmount(0.5);
    connect(amount, &QSlider::valueChanged, this, [this](int value) {
        compare_->setWipePosition(value / 100.0);
        compare_->setOverlayAmount(value / 100.0);
    });
    layout->addWidget(amount);
    auto* label = new QLabel(tr("B 时间 = A 时间 + 偏移（毫秒）"), panel);
    label->setWordWrap(true); layout->addWidget(label);
    compareOffset_ = new QSpinBox(panel);
    compareOffset_->setObjectName(QStringLiteral("compareOffsetSpin"));
    compareOffset_->setRange(-86400000, 86400000);
    connect(compareOffset_, &QSpinBox::valueChanged, this, [this](int value) { compare_->setOffset(value); });
    layout->addWidget(compareOffset_);
    auto* reference = new QPushButton(tr("冻结 B 为参考帧"), panel);
    reference->setObjectName(QStringLiteral("captureReferenceButton"));
    connect(reference, &QPushButton::clicked, this, [this] {
        compare_->captureReference();
        if (compareMode_->currentIndex() == 0) compareMode_->setCurrentIndex(1);
    });
    layout->addWidget(reference);
    auto* clear = new QPushButton(tr("清除 B / 参考帧"), panel);
    connect(clear, &QPushButton::clicked, this, [this] {
        compare_->clearReference(); compare_->clearSecondary(); secondaryAsset_ = -1;
        compareMode_->setCurrentIndex(0); refreshMediaList();
    });
    layout->addWidget(clear);
    auto* help = new QLabel(tr("A 主时钟 / 仅 A 有声。毫秒近似同步，非逐帧锁定；差分为 8-bit 显示域比较。"), panel);
    help->setWordWrap(true); layout->addWidget(help);
    dock->setWidget(panel); addDockWidget(Qt::LeftDockWidgetArea, dock);
    menuBar()->addAction(dock->toggleViewAction());
}

void ReviewWindow::refreshMediaList()
{
    if (!mediaList_) return;
    const int selected = mediaList_->currentRow();
    const QSignalBlocker blocker(mediaList_);
    mediaList_->clear();
    for (int i = 0; i < assets_.size(); ++i) {
        const auto& asset = assets_[i];
        QString role;
        if (i == activeAsset_) role += QStringLiteral("A ");
        if (i == secondaryAsset_) role += QStringLiteral("B ");
        auto* item = new QListWidgetItem(role + QFileInfo(asset.path).fileName(), mediaList_);
        item->setToolTip(asset.path);
    }
    mediaList_->setCurrentRow(selected >= 0 && selected < assets_.size() ? selected : activeAsset_);
}

void ReviewWindow::rememberCurrentMedia()
{
    if (document_.mediaPath.isEmpty()) return;
    if (activeAsset_ < 0) {
        for (int i = 0; i < assets_.size(); ++i)
            if (assets_[i].path == document_.mediaPath && assets_[i].project == projectPath_) {
                activeAsset_ = i; break;
            }
        if (activeAsset_ < 0) {
            assets_.append({document_.mediaPath, projectPath_, 0});
            activeAsset_ = static_cast<int>(assets_.size()) - 1;
        }
    }
    assets_[activeAsset_] = {document_.mediaPath, projectPath_, positionMs_};
    refreshMediaList();
}

bool ReviewWindow::addMedia(const QString& path)
{
    const QFileInfo file(path);
    if (!file.isFile() || !file.isReadable()) return false;
    QString media = file.absoluteFilePath();
    QString project;
    if (media.endsWith(QStringLiteral(".review.json"), Qt::CaseInsensitive)) {
        ReviewDocument loaded; QString error;
        if (!loadDocument(media, loaded, error)) { playbackError(error); return false; }
        project = media; media = loaded.mediaPath;
    }
    for (const auto& asset : assets_)
        if (asset.path == media && asset.project == project) return true;
    if (assets_.size() >= 256) { playbackError(tr("素材列表最多容纳 256 项。")); return false; }
    assets_.append({media, project, 0});
    refreshMediaList();
    return true;
}

bool ReviewWindow::activateMedia(int index)
{
    if (index < 0 || index >= assets_.size()) return false;
    if (index == activeAsset_) return true;
    const auto asset = assets_[index];
    activatingMedia_ = true;
    const bool opened = asset.project.isEmpty() ? openVideo(asset.path) : openProject(asset.project);
    activatingMedia_ = false;
    if (!opened) return false;
    activeAsset_ = index;
    startupPosition_ = asset.position;
    refreshMediaList();
    return true;
}

bool ReviewWindow::compareMedia(int index)
{
    if (index < 0 || index >= assets_.size()) return false;
    if (!compare_->setSecondary(assets_[index].path)) return false;
    secondaryAsset_ = index;
    if (compareMode_->currentIndex() == 0) compareMode_->setCurrentIndex(1);
    refreshMediaList();
    return true;
}

void ReviewWindow::removeSelectedMedia()
{
    const int index = mediaList_->currentRow();
    if (index < 0 || index >= assets_.size()) return;
    if (index == activeAsset_) {
        if (!confirmDiscard()) return;
        retirePlayer(); removeRecovery();
        document_ = {}; projectPath_.clear(); baselineDirty_ = false;
        undoStack_->clear(); resetPlayback(); rebuildNotes(); refreshDirtyState();
        mediaLabel_->clear(); activeAsset_ = -1;
    } else if (activeAsset_ > index) --activeAsset_;
    if (index == secondaryAsset_) {
        compare_->clearSecondary(); secondaryAsset_ = -1; compareMode_->setCurrentIndex(0);
    } else if (secondaryAsset_ > index) --secondaryAsset_;
    assets_.removeAt(index); refreshMediaList();
}

void ReviewWindow::updateCompareView()
{
    for (const char* name : {"fitViewAction", "actualSizeAction", "zoomOutAction", "zoomInAction",
                             "mirrorHorizontalAction", "mirrorVerticalAction"})
        if (auto* action = findChild<QAction*>(QString::fromLatin1(name))) action->setEnabled(!comparisonEnabled_);
    exportFrameAction_->setText(comparisonEnabled_ ? tr("导出 A 视口 PNG…") : tr("导出视口 PNG…"));
    if (!session_ || openFailed_) videoStack_->setCurrentWidget(videoPlaceholder_);
    else videoStack_->setCurrentWidget(comparisonEnabled_ ? static_cast<QWidget*>(compare_) : static_cast<QWidget*>(video_));
}
}
