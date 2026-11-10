#include "review_window.h"

#include <player_sdk_qt/qt_player.h>
#include <player_sdk_qt/video_widget.h>
#include <play_core/media_source.h>

#include <QAbstractItemView>
#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QScreen>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <numeric>
#include <utility>
#include <vector>

namespace review {
namespace {
constexpr int SliderSteps = 1000000;
constexpr int MaxNotes = 10000;
constexpr qsizetype MaxNoteText = 10000;

QString absolutePath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool samePath(const QString& left, const QString& right)
{
    if (left.isEmpty() || right.isEmpty())
        return false;
    const auto normalized = [](const QString& path) {
        const QFileInfo info(path);
        const QString canonical = info.canonicalFilePath();
        return canonical.isEmpty() ? absolutePath(path) : canonical;
    };
#ifdef Q_OS_WIN
    return normalized(left).compare(normalized(right), Qt::CaseInsensitive) == 0;
#else
    return normalized(left) == normalized(right);
#endif
}

QString chooseFile(QWidget* parent, const QString& name, const QString& title,
                   const QString& filter, bool save, const QString& initial = {},
                   const QString& suffix = {})
{
    QFileDialog dialog(parent, title);
    dialog.setObjectName(name);
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setNameFilter(filter);
    dialog.setAcceptMode(save ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    dialog.setFileMode(save ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
    dialog.setDefaultSuffix(suffix);
    if (!initial.isEmpty())
        dialog.selectFile(initial);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return {};
    return absolutePath(dialog.selectedFiles().constFirst());
}

bool editNoteDialog(QWidget* parent, ReviewNote& note, bool adding)
{
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("noteDialog"));
    dialog.setWindowTitle(adding ? QObject::tr("添加标注") : QObject::tr("编辑标注"));
    dialog.resize(440, 320);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* time = new QLabel(formatTime(note.positionMs), &dialog);
    time->setObjectName(QStringLiteral("noteTimeLabel"));
    time->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QObject::tr("时间"), time);
    auto* category = new QComboBox(&dialog);
    category->setObjectName(QStringLiteral("noteCategoryCombo"));
    category->addItems({QStringLiteral("问题"), QStringLiteral("建议"),
                        QStringLiteral("重点"), QStringLiteral("其他")});
    category->setCurrentText(note.category);
    form->addRow(QObject::tr("类别"), category);
    auto* text = new QPlainTextEdit(&dialog);
    text->setObjectName(QStringLiteral("noteTextEdit"));
    text->setPlainText(note.text);
    text->setTabChangesFocus(true);
    form->addRow(QObject::tr("文字"), text);
    layout->addLayout(form);
    auto* validation = new QLabel(&dialog);
    validation->setObjectName(QStringLiteral("noteValidationLabel"));
    validation->setWordWrap(true);
    layout->addWidget(validation);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    buttons->setObjectName(QStringLiteral("noteButtons"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("noteAcceptButton"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("noteCancelButton"));
    layout->addWidget(buttons);
    const auto validate = [text, validation, buttons] {
        const QString value = text->toPlainText();
        const bool empty = value.trimmed().isEmpty();
        const bool tooLong = value.size() > MaxNoteText;
        validation->setText(empty ? QObject::tr("请输入标注文字。")
                                  : tooLong ? QObject::tr("文字最多 10000 个 UTF-16 单元。")
                                            : QString());
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!empty && !tooLong);
    };
    QObject::connect(text, &QPlainTextEdit::textChanged, &dialog, validate);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    validate();
    text->setFocus();
    if (dialog.exec() != QDialog::Accepted)
        return false;
    note.category = category->currentText();
    note.text = text->toPlainText();
    return true;
}

QString requestError(const play_core::PlayerError& error)
{
    return error.message.empty() ? QObject::tr("SDK 拒绝了此操作。")
                                : QString::fromStdString(error.message);
}
} // namespace

ReviewWindow::ReviewWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setObjectName(QStringLiteral("reviewWindow"));
    buildUi();
    // Construct at startup so main can report SDK initialization exceptions.
    player_ = new player_sdk_qt::QtPlayer(this);
    player_->setObjectName(QStringLiteral("reviewPlayer"));
    bindPlayer();
    resetPlayback();
    updateTitle();
}

ReviewWindow::~ReviewWindow()
{
    retirePlayer();
}

void ReviewWindow::buildUi()
{
    if (const auto* display = QGuiApplication::primaryScreen()) {
        const QRect available = display->availableGeometry();
        resize(std::min(1200, std::max(1, available.width() - 32)),
               std::min(760, std::max(1, available.height() - 32)));
        move(available.center() - rect().center());
    } else {
        resize(1200, 760);
    }
    auto* fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    fileMenu->setObjectName(QStringLiteral("fileMenu"));
    auto* openVideoAction = fileMenu->addAction(tr("打开视频…"));
    openVideoAction->setObjectName(QStringLiteral("openVideoAction"));
    openVideoAction->setShortcut(QKeySequence::Open);
    connect(openVideoAction, &QAction::triggered, this, [this] {
        const QString path = chooseFile(this, QStringLiteral("openVideoDialog"), tr("打开视频"),
                                        tr("视频文件 (*.mp4 *.mkv *.avi *.mov *.webm *.wmv *.m4v *.ts);;所有文件 (*)"), false);
        if (!path.isEmpty())
            openVideo(path);
    });
    auto* openProjectAction = fileMenu->addAction(tr("打开标注工程…"));
    openProjectAction->setObjectName(QStringLiteral("openProjectAction"));
    connect(openProjectAction, &QAction::triggered, this, [this] {
        const QString path = chooseFile(this, QStringLiteral("openProjectDialog"), tr("打开标注工程"),
                                        tr("审片工程 (*.review.json);;JSON 文件 (*.json)"), false);
        if (!path.isEmpty())
            openProject(path);
    });
    fileMenu->addSeparator();
    saveAction_ = fileMenu->addAction(tr("保存"));
    saveAction_->setObjectName(QStringLiteral("saveProjectAction"));
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, [this] { saveProject(false); });
    saveAsAction_ = fileMenu->addAction(tr("另存为…"));
    saveAsAction_->setObjectName(QStringLiteral("saveProjectAsAction"));
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction_, &QAction::triggered, this, [this] { saveProject(true); });
    exportAction_ = fileMenu->addAction(tr("导出 CSV…"));
    exportAction_->setObjectName(QStringLiteral("exportCsvAction"));
    connect(exportAction_, &QAction::triggered, this, &ReviewWindow::exportProjectCsv);
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction(tr("退出"));
    quitAction->setObjectName(QStringLiteral("quitAction"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto* noteMenu = menuBar()->addMenu(tr("标注(&N)"));
    noteMenu->setObjectName(QStringLiteral("noteMenu"));
    addNoteAction_ = noteMenu->addAction(tr("添加标注"));
    addNoteAction_->setObjectName(QStringLiteral("addNoteAction"));
    addNoteAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    connect(addNoteAction_, &QAction::triggered, this, &ReviewWindow::addNote);
    editNoteAction_ = noteMenu->addAction(tr("编辑标注"));
    editNoteAction_->setObjectName(QStringLiteral("editNoteAction"));
    connect(editNoteAction_, &QAction::triggered, this, &ReviewWindow::editNote);
    deleteNoteAction_ = noteMenu->addAction(tr("删除标注"));
    deleteNoteAction_->setObjectName(QStringLiteral("deleteNoteAction"));
    connect(deleteNoteAction_, &QAction::triggered, this, &ReviewWindow::deleteNote);
    seekNoteAction_ = noteMenu->addAction(tr("暂停并定位到标注"));
    seekNoteAction_->setObjectName(QStringLiteral("seekNoteAction"));
    connect(seekNoteAction_, &QAction::triggered, this, &ReviewWindow::seekNote);

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("reviewCentralWidget"));
    auto* layout = new QVBoxLayout(central);
    auto* mediaRow = new QHBoxLayout;
    mediaLabel_ = new QLabel(tr("审片工作区"), central);
    mediaLabel_->setObjectName(QStringLiteral("mediaPathLabel"));
    mediaLabel_->setTextFormat(Qt::PlainText);
    mediaLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mediaLabel_->setMinimumWidth(0);
    mediaRow->addWidget(mediaLabel_, 1);
    auto* openVideoButton = new QPushButton(tr("打开视频…"), central);
    openVideoButton->setObjectName(QStringLiteral("openVideoButton"));
    connect(openVideoButton, &QPushButton::clicked, openVideoAction, &QAction::trigger);
    mediaRow->addWidget(openVideoButton);
    layout->addLayout(mediaRow);
    auto* splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setObjectName(QStringLiteral("reviewSplitter"));
    splitter->setChildrenCollapsible(false);
    auto* playback = new QWidget(splitter);
    playback->setMinimumWidth(320);
    auto* playbackLayout = new QVBoxLayout(playback);
    playbackLayout->setContentsMargins(0, 0, 0, 0);
    videoStack_ = new QStackedWidget(playback);
    videoPlaceholder_ = new QLabel(tr("打开本地视频开始审片\n按 Ctrl+O 或点击右上角“打开视频…”"), videoStack_);
    videoPlaceholder_->setObjectName(QStringLiteral("videoPlaceholder"));
    videoPlaceholder_->setAlignment(Qt::AlignCenter);
    videoPlaceholder_->setWordWrap(true);
    videoPlaceholder_->setAutoFillBackground(true);
    videoStack_->addWidget(videoPlaceholder_);
    video_ = new player_sdk_qt::VideoWidget(videoStack_);
    video_->setObjectName(QStringLiteral("reviewVideoWidget"));
    video_->setMinimumSize(320, 180);
    videoStack_->addWidget(video_);
    videoStack_->setCurrentWidget(videoPlaceholder_);
    playbackLayout->addWidget(videoStack_, 1);
    positionSlider_ = new QSlider(Qt::Horizontal, playback);
    positionSlider_->setObjectName(QStringLiteral("positionSlider"));
    positionSlider_->setAccessibleName(tr("播放进度"));
    positionSlider_->setRange(0, SliderSteps);
    playbackLayout->addWidget(positionSlider_);
    auto* controls = new QHBoxLayout;
    playPauseButton_ = new QPushButton(tr("播放"), playback);
    playPauseButton_->setObjectName(QStringLiteral("playPauseButton"));
    connect(playPauseButton_, &QPushButton::clicked, this, &ReviewWindow::togglePlayback);
    controls->addWidget(playPauseButton_);
    timeLabel_ = new QLabel(playback);
    timeLabel_->setObjectName(QStringLiteral("playbackTimeLabel"));
    controls->addWidget(timeLabel_);
    controls->addStretch();
    playbackLayout->addLayout(controls);
    auto* annotation = new QWidget(splitter);
    annotation->setMinimumWidth(300);
    auto* annotationLayout = new QVBoxLayout(annotation);
    annotationLayout->setContentsMargins(0, 0, 0, 0);
    auto* noteTools = new QToolBar(annotation);
    noteTools->setObjectName(QStringLiteral("noteToolBar"));
    noteTools->addAction(addNoteAction_);
    noteTools->addAction(editNoteAction_);
    noteTools->addAction(deleteNoteAction_);
    annotationLayout->addWidget(noteTools);
    emptyNotesLabel_ = new QLabel(annotation);
    emptyNotesLabel_->setObjectName(QStringLiteral("emptyNotesLabel"));
    emptyNotesLabel_->setAlignment(Qt::AlignCenter);
    emptyNotesLabel_->setWordWrap(true);
    annotationLayout->addWidget(emptyNotesLabel_);
    notesTable_ = new QTableWidget(0, 3, annotation);
    notesTable_->setObjectName(QStringLiteral("notesTable"));
    notesTable_->setHorizontalHeaderLabels({tr("时间"), tr("类别"), tr("文字")});
    notesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    notesTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    notesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    notesTable_->setWordWrap(false);
    notesTable_->setAlternatingRowColors(true);
    notesTable_->verticalHeader()->hide();
    notesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    notesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    notesTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    notesTable_->setContextMenuPolicy(Qt::ActionsContextMenu);
    notesTable_->addActions({editNoteAction_, deleteNoteAction_, seekNoteAction_});
    annotationLayout->addWidget(notesTable_, 1);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);
    statusLabel_ = new QLabel(central);
    statusLabel_->setObjectName(QStringLiteral("playbackStatusLabel"));
    statusLabel_->setTextFormat(Qt::PlainText);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);
    errorLabel_ = new QLabel(central);
    errorLabel_->setObjectName(QStringLiteral("playbackErrorLabel"));
    errorLabel_->setTextFormat(Qt::PlainText);
    errorLabel_->setWordWrap(true);
    errorLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    errorLabel_->hide();
    layout->addWidget(errorLabel_);
    setCentralWidget(central);
    statusBar()->setObjectName(QStringLiteral("reviewStatusBar"));
    connect(notesTable_, &QTableWidget::itemSelectionChanged, this, &ReviewWindow::updateControls);
    connect(notesTable_, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        if (seekNoteAction_->isEnabled())
            seekNoteAction_->trigger();
    });
    connect(positionSlider_, &QSlider::sliderReleased, this, &ReviewWindow::seekFromSlider);
    connect(positionSlider_, &QSlider::valueChanged, this, [this] {
        if (positionSlider_->isSliderDown())
            updateTimeDisplay();
        else
            seekFromSlider();
    });
}

void ReviewWindow::bindPlayer()
{
    video_->setPlayer(player_);
    const quint64 generation = playerGeneration_;
    connect(player_, &player_sdk_qt::QtPlayer::stateChanged, this,
            [this, generation](play_core::PlayerState state) {
        if (generation != playerGeneration_)
            return;
        state_ = state;
        switch (state) {
        case play_core::PlayerState::Closed: statusLabel_->setText(tr("媒体已关闭")); break;
        case play_core::PlayerState::Opening: statusLabel_->setText(tr("正在打开视频…")); break;
        case play_core::PlayerState::Ready: statusLabel_->setText(tr("视频已就绪")); break;
        case play_core::PlayerState::Playing: statusLabel_->setText(tr("正在播放")); break;
        case play_core::PlayerState::Paused: statusLabel_->setText(tr("已暂停")); break;
        case play_core::PlayerState::Reconnecting: statusLabel_->setText(tr("正在重新连接…")); break;
        case play_core::PlayerState::Stopped: statusLabel_->setText(tr("已停止")); break;
        case play_core::PlayerState::Closing: statusLabel_->setText(tr("正在关闭媒体…")); break;
        case play_core::PlayerState::Ended: statusLabel_->setText(tr("播放结束，可重新播放")); break;
        case play_core::PlayerState::Failed:
            openFailed_ = true;
            statusLabel_->setText(tr("播放失败，标注仍可编辑和保存"));
            break;
        }
        updateControls();
    });
    connect(player_, &player_sdk_qt::QtPlayer::mediaInfoChanged, this,
            [this, generation](std::shared_ptr<const play_core::MediaInfo> info) {
        if (generation != playerGeneration_)
            return;
        seekableVideo_ = false;
        durationMs_ = 0;
        if (info) {
            // SDK tracks enumerates switchable audio/subtitle tracks; video is
            // reported through dimensions instead of a TrackInfo entry.
            const bool hasVideo = info->width > 0 && info->height > 0;
            seekableVideo_ = hasVideo && info->seekable;
            if (info->duration)
                durationMs_ = std::max<qint64>(0, info->duration->count());
            if (!hasVideo)
                playbackError(tr("媒体不含视频轨道，无法添加或定位标注。"));
            else if (!info->seekable)
                playbackError(tr("该视频不可定位，无法添加或定位标注。"));
        }
        updateTimeDisplay();
        updateControls();
    });
    connect(player_, &player_sdk_qt::QtPlayer::positionChanged, this,
            [this, generation](qint64 milliseconds) {
        if (generation != playerGeneration_)
            return;
        positionMs_ = std::max<qint64>(0, milliseconds);
        if (!positionSlider_->isSliderDown()) {
            const QSignalBlocker blocker(positionSlider_);
            const long double fraction = durationMs_ > 0
                ? static_cast<long double>(std::min(positionMs_, durationMs_)) / durationMs_ : 0;
            positionSlider_->setValue(static_cast<int>(fraction * SliderSteps));
        }
        updateTimeDisplay();
    });
    connect(player_, &player_sdk_qt::QtPlayer::operationFinished, this,
            [this, generation](const play_core::OperationResult& result) {
        if (generation != playerGeneration_ || result.session != session_)
            return;
        const QString action = pendingOperations_.take(result.operation);
        if (result.status == play_core::OperationStatus::Failed) {
            if (result.operation == openOperation_) {
                openFailed_ = true;
                updateControls();
            }
            playbackError(tr("%1失败：%2").arg(action.isEmpty() ? tr("播放操作") : action,
                                                   requestError(result.error)));
        }
    });
    connect(player_, &player_sdk_qt::QtPlayer::errorOccurred, this,
            [this, generation](const QString& message) {
        if (generation == playerGeneration_)
            playbackError(message);
    });
}

void ReviewWindow::retirePlayer()
{
    ++playerGeneration_;
    if (player_) {
        disconnect(player_, nullptr, this, nullptr);
        video_->setPlayer(nullptr);
        delete player_;
        player_ = nullptr;
    }
    pendingOperations_.clear();
}

void ReviewWindow::resetPlayback()
{
    session_ = 0;
    openOperation_ = 0;
    state_ = play_core::PlayerState::Closed;
    positionMs_ = 0;
    durationMs_ = 0;
    seekableVideo_ = false;
    openFailed_ = false;
    pendingOperations_.clear();
    {
        const QSignalBlocker blocker(positionSlider_);
        positionSlider_->setSliderDown(false);
        positionSlider_->setValue(0);
    }
    errorLabel_->clear();
    errorLabel_->hide();
    videoStack_->setCurrentWidget(videoPlaceholder_);
    statusLabel_->setText(tr("请选择本地视频或标注工程"));
    updateTimeDisplay();
    updateControls();
}

bool ReviewWindow::startMedia()
{
    // Qt signals do not all carry a session ID. Replace every used player;
    // the generation guard rejects already-queued signals from the old file.
    if (session_ != 0 || state_ != play_core::PlayerState::Closed)
        retirePlayer();
    resetPlayback();
    const QFileInfo media(document_.mediaPath);
    if (!media.isFile() || !media.isReadable()) {
        statusLabel_->setText(tr("媒体缺失或不可读：%1。标注仍可编辑、保存和导出。")
                                 .arg(QDir::toNativeSeparators(document_.mediaPath)));
        return false;
    }
    try {
        if (!player_) {
            player_ = new player_sdk_qt::QtPlayer(this);
            player_->setObjectName(QStringLiteral("reviewPlayer"));
            bindPlayer();
        }
    } catch (const std::exception& exception) {
        openFailed_ = true;
        showError(tr("无法初始化播放器"), QString::fromUtf8(exception.what()));
        updateControls();
        return false;
    } catch (...) {
        openFailed_ = true;
        showError(tr("无法初始化播放器"), tr("SDK 初始化发生未知异常。"));
        updateControls();
        return false;
    }
    state_ = play_core::PlayerState::Opening;
    statusLabel_->setText(tr("正在打开视频…"));
    updateControls();
    const auto request = player_->open(QUrl::fromLocalFile(document_.mediaPath));
    session_ = request.session;
    openOperation_ = request.operation;
    if (!submit(request, tr("打开视频"))) {
        openFailed_ = true;
        state_ = play_core::PlayerState::Failed;
        updateControls();
        return false;
    }
    videoStack_->setCurrentWidget(video_);
    // Opening explicitly supports queued playback commands in the public SDK.
    submit(player_->play(), tr("播放"));
    return true;
}

bool ReviewWindow::openVideo(const QString& path)
{
    if (path.isEmpty())
        return false;
    const QString media = absolutePath(path);
    const QFileInfo info(media);
    if (!info.isFile() || !info.isReadable()) {
        showError(tr("无法打开视频"), tr("视频不存在或不可读：%1").arg(QDir::toNativeSeparators(media)));
        return false;
    }
    if (!confirmDiscard())
        return false;
    document_ = ReviewDocument{media, {}};
    ++documentGeneration_;
    projectPath_.clear();
    mediaLabel_->setText(info.fileName());
    mediaLabel_->setToolTip(QDir::toNativeSeparators(media));
    setDirty(true);
    rebuildNotes();
    return startMedia();
}

bool ReviewWindow::openProject(const QString& path)
{
    if (path.isEmpty())
        return false;
    ReviewDocument loaded;
    QString error;
    const QString project = absolutePath(path);
    if (!loadDocument(project, loaded, error)) {
        showError(tr("无法载入工程"), error);
        return false;
    }
    if (!confirmDiscard())
        return false;
    document_ = std::move(loaded);
    ++documentGeneration_;
    projectPath_ = project;
    mediaLabel_->setText(QFileInfo(document_.mediaPath).fileName());
    mediaLabel_->setToolTip(QDir::toNativeSeparators(document_.mediaPath));
    setDirty(false);
    rebuildNotes();
    // Loading annotations succeeds even when their external media is missing.
    startMedia();
    return true;
}

bool ReviewWindow::canControlMedia() const
{
    if (!player_ || !session_ || openFailed_)
        return false;
    return state_ == play_core::PlayerState::Ready || state_ == play_core::PlayerState::Playing
        || state_ == play_core::PlayerState::Paused || state_ == play_core::PlayerState::Stopped;
}

bool ReviewWindow::canSeek() const
{
    return canControlMedia() && seekableVideo_;
}

void ReviewWindow::updateControls()
{
    const bool hasDocument = !document_.mediaPath.isEmpty();
    const bool selected = selectedNote() >= 0;
    const bool replay = player_ && session_ && state_ == play_core::PlayerState::Ended && !openFailed_;
    playPauseButton_->setEnabled(canControlMedia() || replay);
    playPauseButton_->setText(state_ == play_core::PlayerState::Playing ? tr("暂停")
                              : replay ? tr("重新播放") : tr("播放"));
    positionSlider_->setEnabled(canSeek() && durationMs_ > 0);
    addNoteAction_->setEnabled(canSeek() && document_.notes.size() < MaxNotes);
    editNoteAction_->setEnabled(selected);
    deleteNoteAction_->setEnabled(selected);
    seekNoteAction_->setEnabled(selected && canSeek());
    saveAction_->setEnabled(hasDocument);
    saveAsAction_->setEnabled(hasDocument);
    exportAction_->setEnabled(hasDocument);
    emptyNotesLabel_->setVisible(document_.notes.isEmpty());
    if (document_.notes.isEmpty()) {
        emptyNotesLabel_->setText(!hasDocument ? tr("暂无标注")
                                  : canSeek() ? tr("点击“添加标注”或按 Ctrl+M，记录当前画面。")
                                              : tr("媒体不可定位，无法添加时间点标注。"));
    }
    notesTable_->setToolTip(canSeek() ? tr("双击标注可暂停并定位；使用编辑按钮修改文字和类别。")
                                     : tr("当前无法定位；已有标注仍可编辑、删除、保存和导出。"));
}

qint64 ReviewWindow::sliderPositionMs() const
{
    return static_cast<qint64>(static_cast<long double>(durationMs_) * positionSlider_->value() / SliderSteps);
}

void ReviewWindow::updateTimeDisplay()
{
    const qint64 shown = positionSlider_->isSliderDown() ? sliderPositionMs() : positionMs_;
    timeLabel_->setText(tr("%1 / %2").arg(formatTime(shown),
                                         durationMs_ > 0 ? formatTime(durationMs_) : tr("未知时长")));
}

void ReviewWindow::setDirty(bool dirty)
{
    dirty_ = dirty;
    updateTitle();
}

void ReviewWindow::updateTitle()
{
    const QString name = !projectPath_.isEmpty() ? QFileInfo(projectPath_).fileName()
                         : !document_.mediaPath.isEmpty() ? QFileInfo(document_.mediaPath).fileName()
                                                        : tr("未命名工程");
    setWindowTitle(tr("%1[*] — MyPlayer Review").arg(name));
    setWindowModified(dirty_);
}

int ReviewWindow::selectedNote() const
{
    const int row = notesTable_->currentRow();
    if (row < 0 || !notesTable_->item(row, 0) || !notesTable_->item(row, 0)->isSelected())
        return -1;
    const int index = notesTable_->item(row, 0)->data(Qt::UserRole).toInt();
    return index >= 0 && index < document_.notes.size() ? index : -1;
}

void ReviewWindow::rebuildNotes(int selected)
{
    const QSignalBlocker blocker(notesTable_);
    notesTable_->clearContents();
    notesTable_->setRowCount(static_cast<int>(document_.notes.size()));
    std::vector<int> order(static_cast<std::size_t>(document_.notes.size()));
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [this](int left, int right) {
        return document_.notes.at(left).positionMs < document_.notes.at(right).positionMs;
    });
    for (int row = 0; row < static_cast<int>(order.size()); ++row) {
        const int index = order[static_cast<std::size_t>(row)];
        const auto& note = document_.notes.at(index);
        auto* time = new QTableWidgetItem(formatTime(note.positionMs));
        time->setData(Qt::UserRole, index);
        notesTable_->setItem(row, 0, time);
        notesTable_->setItem(row, 1, new QTableWidgetItem(note.category));
        notesTable_->setItem(row, 2, new QTableWidgetItem(note.text));
        if (index == selected)
            notesTable_->selectRow(row);
    }
    updateControls();
}

bool ReviewWindow::confirmDiscard()
{
    if (!dirty_)
        return true;
    QMessageBox dialog(QMessageBox::Warning, tr("未保存的工程"),
                       tr("当前工程有未保存的更改。是否先保存？"),
                       QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    dialog.setObjectName(QStringLiteral("unsavedChangesDialog"));
    dialog.setDefaultButton(QMessageBox::Save);
    dialog.setEscapeButton(QMessageBox::Cancel);
    dialog.button(QMessageBox::Save)->setObjectName(QStringLiteral("saveChangesButton"));
    dialog.button(QMessageBox::Discard)->setObjectName(QStringLiteral("discardChangesButton"));
    dialog.button(QMessageBox::Cancel)->setObjectName(QStringLiteral("cancelChangesButton"));
    const int result = dialog.exec();
    if (result == QMessageBox::Save)
        return saveProject(false);
    return result == QMessageBox::Discard;
}

bool ReviewWindow::saveProject(bool saveAs)
{
    if (document_.mediaPath.isEmpty())
        return false;
    QString path = projectPath_;
    if (saveAs || path.isEmpty()) {
        const QString initial = path.isEmpty() ? document_.mediaPath + QStringLiteral(".review.json") : path;
        path = chooseFile(this, QStringLiteral("saveProjectDialog"), tr("保存标注工程"),
                          tr("审片工程 (*.review.json)"), true, initial, QStringLiteral("review.json"));
        if (path.isEmpty())
            return false;
    }
    QString error;
    if (!saveDocument(path, document_, error)) {
        showError(tr("保存失败"), error);
        return false;
    }
    projectPath_ = path;
    setDirty(false);
    statusBar()->showMessage(tr("工程已保存：%1").arg(QDir::toNativeSeparators(path)), 5000);
    return true;
}

void ReviewWindow::exportProjectCsv()
{
    if (document_.mediaPath.isEmpty())
        return;
    const QString initial = projectPath_.isEmpty() ? document_.mediaPath + QStringLiteral(".csv")
                                                  : projectPath_ + QStringLiteral(".csv");
    const QString path = chooseFile(this, QStringLiteral("exportCsvDialog"), tr("导出 CSV"),
                                    tr("CSV 文件 (*.csv)"), true, initial, QStringLiteral("csv"));
    if (path.isEmpty())
        return;
    if (samePath(path, projectPath_)) {
        showError(tr("导出失败"), tr("CSV 不能覆盖当前标注工程。请选择其他文件。"));
        return;
    }
    QString error;
    if (!exportCsv(path, document_, error)) {
        showError(tr("导出失败"), error);
        return;
    }
    statusBar()->showMessage(tr("CSV 已导出：%1").arg(QDir::toNativeSeparators(path)), 5000);
}

void ReviewWindow::addNote()
{
    if (!canSeek() || document_.notes.size() >= MaxNotes)
        return;
    // Capture SDK time before entering the modal input dialog.
    ReviewNote note{positionMs_, QStringLiteral("问题"), {}};
    const quint64 generation = documentGeneration_;
    if (!editNoteDialog(this, note, true) || generation != documentGeneration_)
        return;
    document_.notes.append(std::move(note));
    setDirty(true);
    rebuildNotes(static_cast<int>(document_.notes.size()) - 1);
}

void ReviewWindow::editNote()
{
    const int index = selectedNote();
    if (index < 0)
        return;
    ReviewNote note = document_.notes.at(index);
    const quint64 generation = documentGeneration_;
    if (!editNoteDialog(this, note, false) || generation != documentGeneration_)
        return;
    if (note.category == document_.notes.at(index).category && note.text == document_.notes.at(index).text)
        return;
    document_.notes[index] = std::move(note);
    setDirty(true);
    rebuildNotes(index);
}

void ReviewWindow::deleteNote()
{
    const int index = selectedNote();
    if (index < 0)
        return;
    document_.notes.removeAt(index);
    setDirty(true);
    rebuildNotes();
}

void ReviewWindow::seekNote()
{
    const int index = selectedNote();
    if (index < 0 || !canSeek())
        return;
    errorLabel_->clear();
    errorLabel_->hide();
    if (!submit(player_->setPaused(true), tr("暂停")))
        return;
    submit(player_->seek(document_.notes.at(index).positionMs), tr("定位标注"));
}

void ReviewWindow::togglePlayback()
{
    if (player_ && state_ == play_core::PlayerState::Ended) {
        startMedia();
        return;
    }
    if (!canControlMedia())
        return;
    errorLabel_->clear();
    errorLabel_->hide();
    if (state_ == play_core::PlayerState::Playing)
        submit(player_->setPaused(true), tr("暂停"));
    else if (state_ == play_core::PlayerState::Paused)
        submit(player_->setPaused(false), tr("恢复播放"));
    else
        submit(player_->play(), tr("播放"));
}

void ReviewWindow::seekFromSlider()
{
    if (!canSeek() || durationMs_ <= 0)
        return;
    errorLabel_->clear();
    errorLabel_->hide();
    submit(player_->seek(sliderPositionMs()), tr("定位"));
    updateTimeDisplay();
}

bool ReviewWindow::submit(const play_core::Request& request, const QString& action)
{
    if (!request) {
        playbackError(tr("%1失败：%2").arg(action, requestError(request.error)));
        return false;
    }
    pendingOperations_.insert(request.operation, action);
    return true;
}

void ReviewWindow::playbackError(const QString& message)
{
    errorLabel_->setText(tr("错误：%1").arg(message));
    errorLabel_->show();
}

void ReviewWindow::showError(const QString& title, const QString& message)
{
    QMessageBox dialog(QMessageBox::Critical, title, message, QMessageBox::Ok, this);
    dialog.setObjectName(QStringLiteral("reviewErrorDialog"));
    dialog.setTextFormat(Qt::PlainText);
    dialog.exec();
}

void ReviewWindow::closeEvent(QCloseEvent* event)
{
    if (confirmDiscard())
        event->accept();
    else
        event->ignore();
}

} // namespace review
